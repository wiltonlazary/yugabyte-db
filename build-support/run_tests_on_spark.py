#!/usr/bin/env python2

# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

"""
Run YugaByte tests on Spark using PySpark.

Example (mostly useful during testing this script):

cd ~/code/yugabyte
spark-submit --driver-cores 8 \
  build-support/run_tests_on_spark.py \
  --build-root build/debug-gcc-dynamic-community \
  --cpp \
  --verbose \
  --reports-dir /tmp \
  --cpp_test_program_regexp '.*redisserver.*' \
  --write_report \
  --save_report_to_build_dir

Also adding --spark-master-url=local helps with local debugging.
"""

import argparse
import getpass
import glob
import gzip
import json
import logging
import os
import pwd
import random
import re
import socket
import subprocess
import sys
import time
import traceback
import tempfile
import errno
import signal
from collections import defaultdict

BUILD_SUPPORT_DIR = os.path.dirname(os.path.realpath(__file__))
YB_PYTHONPATH_ENTRY = os.path.realpath(os.path.join(BUILD_SUPPORT_DIR, '..', 'python'))

# An upper bound on a single test's running time. In practice there are multiple other timeouts
# that should be triggered earlier.
TEST_TIMEOUT_UPPER_BOUND_SEC = 35 * 60

# We wait for a special "flag file" to appear indicating that the test has started running, which is
# created by the about_to_start_running_test in common-test-env.sh. If this does not happen within
# this amount of time, we terminate the run-test.sh script. This should prevent tests getting stuck
# for a long time in macOS builds.
TIME_SEC_TO_START_RUNNING_TEST = 30

# Additional Python module path elements that we've successfully added (that were not already in
# sys.path).
pythonpath_adjustments = []


def add_pythonpath_entry(entry):
    if entry not in sys.path:
        sys.path.append(entry)
        pythonpath_adjustments.append(entry)


def adjust_pythonpath():
    add_pythonpath_entry(YB_PYTHONPATH_ENTRY)


def wait_for_path_to_exist(target_path):
    if os.path.exists(target_path):
        return
    waited_for_sec = 0
    start_time_sec = time.time()
    printed_msg_at_sec = 0
    MSG_PRINT_INTERVAL_SEC = 5.0
    TIMEOUT_SEC = 120
    while not os.path.exists(target_path):
        current_time_sec = time.time()
        if current_time_sec - printed_msg_at_sec >= MSG_PRINT_INTERVAL_SEC:
            sys.stderr.write("Path '%s' does not exist, waiting\n" % target_path)
            printed_msg_at_sec = current_time_sec
        if current_time_sec - start_time_sec >= TIMEOUT_SEC:
            raise IOError(
                "Timed out after %.1f seconds waiting for path to exist: %s" % (
                    current_time_sec - start_time_sec, target_path
                ))
        time.sleep(0.1)
    elapsed_time = time.time() - start_time_sec
    sys.stderr.write("Waited for %.1f seconds for the path '%s' to appear\n" % (
        elapsed_time, target_path
    ))


adjust_pythonpath()

# Wait for our module path, which may be on NFS, to be mounted.
wait_for_path_to_exist(YB_PYTHONPATH_ENTRY)

from yb import yb_dist_tests  # noqa
from yb import command_util  # noqa
from yb.common_util import set_to_comma_sep_str, get_bool_env_var, is_macos  # noqa


# Special Jenkins environment variables. They are propagated to tasks running in a distributed way
# on Spark.
JENKINS_ENV_VARS = [
    "BUILD_TYPE",
    "BUILD_ID",
    "BUILD_NUMBER",
    "BUILD_TAG",
    "BUILD_URL",
    "CVS_BRANCH",
    "EXECUTOR_NUMBER",
    "GIT_BRANCH",
    "GIT_COMMIT",
    "GIT_URL",
    "JAVA_HOME",
    "JENKINS_URL",
    "JOB_NAME",
    "NODE_NAME",
    "SVN_REVISION",
    "WORKSPACE",
    ]

# In addition, all variables with names starting with the following prefix are propagated.
PROPAGATED_ENV_VAR_PREFIX = 'YB_'

# This directory inside $BUILD_ROOT contains files listing all C++ tests (one file per test
# program).
#
# This must match the constant with the same name in common-test-env.sh.
LIST_OF_TESTS_DIR_NAME = 'list_of_tests'

# Global variables.
propagated_env_vars = {}
global_conf_dict = None

SPARK_URLS = {
    'linux_default': os.getenv(
        'YB_SPARK_URL_LINUX_DEFAULT',
        'spark://spark-for-yugabyte-linux-default.example.com:7077'),
    'linux_asan_tsan': os.getenv(
        'YB_SPARK_URL_LINUX_ASAN_TSAN',
        'spark://spark-for-yugabyte-linux-asan-tsan.example.com:7077'),
    'macos': os.getenv(
        'YB_SPARK_URL_MACOS',
        'spark://spark-for-yugabyte-macos.example.com:7077'),
}

# This has to match what we output in run-test.sh if YB_LIST_CTEST_TESTS_ONLY is set.
CTEST_TEST_PROGRAM_RE = re.compile(r'^.* ctest test: \"(.*)\"$')

spark_context = None

# Non-gtest tests and tests with internal dependencies that we should run in one shot. This almost
# duplicates a  from common-test-env.sh, but that is probably OK since we should not be adding new
# such tests.
ONE_SHOT_TESTS = set([
        'merge_test',
        'c_test',
        'compact_on_deletion_collector_test',
        'db_sanity_test',
        'tests-rocksdb/thread_local_test'])

HASH_COMMENT_RE = re.compile('#.*$')

# Number of failures of any particular task before giving up on the job. The total number of
# failures spread across different tasks will not cause the job to fail; a particular task has to
# fail this number of attempts. Should be greater than or equal to 1. Number of allowed retries =
# this value - 1.
SPARK_TASK_MAX_FAILURES = 100

verbose = False

g_spark_master_url_override = None


def get_sys_path_info_str():
    return 'Host: %s, pythonpath_adjustments=%s, sys.path entries: %s' % (
        socket.gethostname(),
        repr(pythonpath_adjustments),
        ', '.join([
            '"%s" (%s)' % (path_entry, "exists" if os.path.exists(path_entry) else "does NOT exist")
            for path_entry in sys.path
        ])
    )


def is_pid_running(pid):
    import psutil
    try:
        process = psutil.Process(pid)
        return process.status() != psutil.STATUS_ZOMBIE
    except psutil.NoSuchProcess:
        # Not running.
        return False


def delete_if_exists_log_errors(file_path):
    if os.path.exists(file_path):
        try:
            os.remove(file_path)
        except OSError, os_error:
            logging.error("Error deleting file %s: %s", file_path, os_error)


# Initializes the spark context. The details list will be incorporated in the Spark application
# name visible in the Spark web UI.
def init_spark_context(details=[]):
    global spark_context
    if spark_context:
        return
    build_type = yb_dist_tests.global_conf.build_type
    from pyspark import SparkContext
    # We sometimes fail tasks due to unsynchronized clocks, so we should tolerate a fair number of
    # retries.
    # https://stackoverflow.com/questions/26260006/are-failed-tasks-resubmitted-in-apache-spark
    # NOTE: we never retry failed tests to avoid hiding bugs. This failure tolerance mechanism
    #       is just for the resilience of the test framework itself.
    SparkContext.setSystemProperty('spark.task.maxFailures', str(SPARK_TASK_MAX_FAILURES))

    spark_master_url = g_spark_master_url_override
    if spark_master_url is None:
        if is_macos():
            logging.info("This is macOS, using the macOS Spark cluster")
            spark_master_url = SPARK_URLS['macos']
        elif yb_dist_tests.global_conf.build_type in ['asan', 'tsan']:
            logging.info("Using a separate Spark cluster for ASAN and TSAN tests")
            spark_master_url = SPARK_URLS['linux_asan_tsan']
        else:
            logging.info("Using the regular Spark cluster for non-ASAN/TSAN tests")
            spark_master_url = SPARK_URLS['linux_default']

    logging.info("Spark master URL: %s", spark_master_url)
    spark_master_url = os.environ.get('YB_SPARK_MASTER_URL', spark_master_url)
    details += [
        'user: {}'.format(getpass.getuser()),
        'build type: {}'.format(build_type)
        ]

    if 'BUILD_URL' in os.environ:
        details.append('URL: {}'.format(os.environ['BUILD_URL']))

    spark_context = SparkContext(spark_master_url, "YB tests ({})".format(', '.join(details)))
    spark_context.addPyFile(yb_dist_tests.__file__)


def set_global_conf_for_spark_jobs():
    global global_conf_dict
    global_conf_dict = vars(yb_dist_tests.global_conf)


def get_bash_path():
    if sys.platform == 'darwin':
        return '/usr/local/bin/bash'
    return '/bin/bash'


def parallel_run_test(test_descriptor_str):
    """
    This is invoked in parallel to actually run tests.
    """
    adjust_pythonpath()
    wait_for_path_to_exist(YB_PYTHONPATH_ENTRY)
    try:
        from yb import yb_dist_tests, command_util
    except ImportError as ex:
        raise ImportError("%s. %s" % (ex.message, get_sys_path_info_str()))

    global_conf = yb_dist_tests.set_global_conf_from_dict(global_conf_dict)
    global_conf.set_env(propagated_env_vars)
    wait_for_path_to_exist(global_conf.build_root)
    yb_dist_tests.global_conf = global_conf
    test_descriptor = yb_dist_tests.TestDescriptor(test_descriptor_str)

    # This is saved in the test result file by process_test_result.py.
    os.environ['YB_TEST_DESCRIPTOR_STR'] = test_descriptor_str

    os.environ['YB_TEST_ATTEMPT_INDEX'] = str(test_descriptor.attempt_index)
    os.environ['build_type'] = global_conf.build_type
    os.environ['YB_RUNNING_TEST_ON_SPARK'] = '1'
    os.environ['BUILD_ROOT'] = global_conf.build_root

    test_started_running_flag_file = os.path.join(
            tempfile.gettempdir(),
            'yb_test_started_running_flag_file_%d_%s' % (
                    os.getpid(),
                    ''.join('%09d' % random.randrange(0, 1000000000) for i in xrange(4))))

    os.environ['YB_TEST_STARTED_RUNNING_FLAG_FILE'] = test_started_running_flag_file
    os.environ['YB_TEST_EXTRA_ERROR_LOG_PATH'] = test_descriptor.error_output_path

    yb_dist_tests.wait_for_clock_sync()

    # We could use "run_program" here, but it collects all the output in memory, which is not
    # ideal for a large amount of test log output. The "tee" part also makes the output visible in
    # the standard error of the Spark task as well, which is sometimes helpful for debugging.
    def run_test():
        start_time_sec = time.time()
        runner_oneline = 'set -o pipefail; "%s" %s 2>&1 | tee "%s"; exit ${PIPESTATUS[0]}' % (
            global_conf.get_run_test_script_path(),
            test_descriptor.args_for_run_test,
            test_descriptor.error_output_path
        )
        process = subprocess.Popen(
            [get_bash_path(), '-c', runner_oneline]
        )

        found_flag_file = False
        while is_pid_running(process.pid):
            elapsed_time_sec = time.time() - start_time_sec
            termination_reason = None
            if elapsed_time_sec > TEST_TIMEOUT_UPPER_BOUND_SEC:
                termination_reason = 'ran longer than %d seconds' % TEST_TIMEOUT_UPPER_BOUND_SEC

            failed_to_launch = False
            if not found_flag_file:
                if os.path.exists(test_started_running_flag_file):
                    found_flag_file = True
                elif elapsed_time_sec > TIME_SEC_TO_START_RUNNING_TEST:
                    termination_reason = (
                        'could not start running the test in %d seconds (file %s not created). '
                        'Ran command: {{ %s }}.'
                    ) % (
                        TIME_SEC_TO_START_RUNNING_TEST,
                        test_started_running_flag_file,
                        runner_oneline
                    )
                    failed_to_launch = True

            if termination_reason:
                error_msg = "Test %s is being terminated (ran for %.1f seconds), reason: %s" % (
                    test_descriptor, elapsed_time_sec, termination_reason
                )
                logging.info(error_msg)
                try:
                    os.kill(process.pid, signal.SIGKILL)
                except OSError, os_error:
                    if os_error.errno == errno.ESRCH:
                        logging.info(
                            "Process with pid %d disappeared suddenly, that's OK",
                            process.pid)
                        pass
                    raise os_error

                if failed_to_launch:
                    # This exception should bubble up to Spark and cause it to hopefully re-run the
                    # test one some other node.
                    raise RuntimeError(error_msg)
                break

            time.sleep(0.5)

        exit_code = process.wait()

        elapsed_time_sec = time.time() - start_time_sec
        logging.info("Test {} ran on {} in %.1f seconds, rc={}".format(
            test_descriptor, socket.gethostname(), exit_code))
        return exit_code, elapsed_time_sec

    try:
        exit_code, elapsed_time_sec = run_test()
        error_output_path = test_descriptor.error_output_path

        failed_without_output = False
        if os.path.isfile(error_output_path) and os.path.getsize(error_output_path) == 0:
            if exit_code == 0:
                # Test succeeded, no error output.
                os.remove(error_output_path)
            else:
                # Test failed without any output! Re-run with "set -x" to diagnose.
                os.environ['YB_DEBUG_RUN_TEST'] = '1'
                exit_code, elapsed_time_sec = run_test()
                del os.environ['YB_DEBUG_RUN_TEST']
                # Also mark this in test results.
                failed_without_output = True

        return yb_dist_tests.TestResult(
                exit_code=exit_code,
                test_descriptor=test_descriptor,
                elapsed_time_sec=elapsed_time_sec,
                failed_without_output=failed_without_output)
    finally:
        delete_if_exists_log_errors(test_started_running_flag_file)


def parallel_list_test_descriptors(rel_test_path):
    """
    This is invoked in parallel to list all individual tests within our C++ test programs. Without
    this, listing all gtest tests across 330 test programs might take about 5 minutes on TSAN and 2
    minutes in debug.
    """
    adjust_pythonpath()
    wait_for_path_to_exist(YB_PYTHONPATH_ENTRY)
    try:
        from yb import yb_dist_tests, command_util
    except ImportError as ex:
        raise ImportError("%s. %s" % (ex.message, get_sys_path_info_str()))
    global_conf = yb_dist_tests.set_global_conf_from_dict(global_conf_dict)
    global_conf.set_env(propagated_env_vars)
    wait_for_path_to_exist(global_conf.build_root)
    list_tests_cmd_line = [
            os.path.join(global_conf.build_root, rel_test_path), '--gtest_list_tests']

    try:
        prog_result = command_util.run_program(list_tests_cmd_line)
    except OSError, ex:
        logging.error("Failed running the command: %s", list_tests_cmd_line)
        raise

    # --gtest_list_tests gives us the following output format:
    #  TestSplitArgs.
    #    Simple
    #    SimpleWithSpaces
    #    SimpleWithQuotes
    #    BadWithQuotes
    #    Empty
    #    Error
    #    BloomFilterReverseCompatibility
    #    BloomFilterWrapper
    #    PrefixExtractorFullFilter
    #    PrefixExtractorBlockFilter
    #    PrefixScan
    #    OptimizeFiltersForHits
    #  BloomStatsTestWithParam/BloomStatsTestWithParam.
    #    BloomStatsTest/0  # GetParam() = (true, true)
    #    BloomStatsTest/1  # GetParam() = (true, false)
    #    BloomStatsTest/2  # GetParam() = (false, false)
    #    BloomStatsTestWithIter/0  # GetParam() = (true, true)
    #    BloomStatsTestWithIter/1  # GetParam() = (true, false)
    #    BloomStatsTestWithIter/2  # GetParam() = (false, false)

    current_test = None
    test_descriptors = []
    test_descriptor_prefix = rel_test_path + yb_dist_tests.TEST_DESCRIPTOR_SEPARATOR
    for line in prog_result.stdout.split("\n"):
        if ('Starting tracking the heap' in line or 'Dumping heap profile to' in line):
            continue
        line = line.rstrip()
        trimmed_line = HASH_COMMENT_RE.sub('', line.strip()).strip()
        if line.startswith('  '):
            test_descriptors.append(test_descriptor_prefix + current_test + trimmed_line)
        else:
            current_test = trimmed_line

    return test_descriptors


def get_username():
    try:
        return os.getlogin()
    except OSError, ex:
        logging.warning(("Got an OSError trying to get the current user name, " +
                         "trying a workaround: {}").format(ex))
        # https://github.com/gitpython-developers/gitpython/issues/39
        try:
            return pwd.getpwuid(os.getuid()).pw_name
        except KeyError, ex:
            user_from_env = os.getenv('USER')
            if user_from_env:
                return user_from_env
            id_output = subprocess.check_output('id').strip()
            ID_OUTPUT_RE = re.compile(r'^uid=\d+[(]([^)]+)[)]\s.*')
            match = ID_OUTPUT_RE.match(id_output)
            if match:
                return match.group(1)
            logging.warning(
                "Could not get user name from the environment, and could not parse 'id' output: %s",
                id_output)
            raise ex


def get_jenkins_job_name():
    return os.environ.get('JOB_NAME', None)


def get_jenkins_job_name_path_component():
    jenkins_job_name = get_jenkins_job_name()
    if jenkins_job_name:
        return "job_" + jenkins_job_name
    else:
        return "unknown_jenkins_job"


def get_report_parent_dir(report_base_dir):
    """
    @return a directory to store build report, relative to the given base directory. Path components
            are based on build type, Jenkins job name, etc.
    """
    return os.path.join(report_base_dir,
                        yb_dist_tests.global_conf.build_type,
                        get_jenkins_job_name_path_component())


def save_json_to_paths(short_description, json_data, output_paths, should_gzip=False):
    """
    Saves the given JSON-friendly data structure to the list of paths (exact copy at each path),
    optionally gzipping the output.
    """
    json_data_str = json.dumps(json_data, sort_keys=True, indent=2) + "\n"

    for output_path in output_paths:
        if output_path is None:
            continue

        assert output_path.endswith('.json'), \
            "Expected output path to end with .json: {}".format(output_path)
        final_output_path = output_path + ('.gz' if should_gzip else '')
        logging.info("Saving {} to {}".format(short_description, final_output_path))
        if should_gzip:
            with gzip.open(final_output_path, 'wb') as output_file:
                output_file.write(json_data_str)
        else:
            with open(final_output_path, 'w') as output_file:
                output_file.write(json_data_str)


def save_report(report_base_dir, results, total_elapsed_time_sec, spark_succeeded,
                save_to_build_dir=False):
    historical_report_path = None
    global_conf = yb_dist_tests.global_conf

    if report_base_dir:
        historical_report_parent_dir = get_report_parent_dir(report_base_dir)

        if not os.path.isdir(historical_report_parent_dir):
            try:
                os.makedirs(historical_report_parent_dir)
            except OSError as exc:
                if exc.errno == errno.EEXIST and os.path.isdir(historical_report_parent_dir):
                    pass
                raise

        try:
            username = get_username()
        except:  # noqa
            logging.error("Could not get username, using 'unknown_user':\n%s",
                          traceback.format_exc())
            username = "unknown_user"

        historical_report_path = os.path.join(
                historical_report_parent_dir,
                '{}_{}__user_{}__build_{}.json'.format(
                    global_conf.build_type,
                    time.strftime('%Y-%m-%dT%H_%M_%S'),
                    username,
                    get_jenkins_job_name_path_component(),
                    os.environ.get('BUILD_ID', 'unknown')))

    test_reports_by_descriptor = {}
    for result in results:
        test_descriptor = result.test_descriptor
        test_report_dict = dict(
            elapsed_time_sec=result.elapsed_time_sec,
            exit_code=result.exit_code,
            language=test_descriptor.language
        )
        test_reports_by_descriptor[test_descriptor.descriptor_str] = test_report_dict
        if test_descriptor.error_output_path and os.path.isfile(test_descriptor.error_output_path):
            test_report_dict['error_output_path'] = test_descriptor.error_output_path

    jenkins_env_var_values = {}
    for jenkins_env_var_name in JENKINS_ENV_VARS:
        if jenkins_env_var_name in os.environ:
            jenkins_env_var_values[jenkins_env_var_name] = os.environ[jenkins_env_var_name]

    report = dict(
        conf=vars(yb_dist_tests.global_conf),
        total_elapsed_time_sec=total_elapsed_time_sec,
        spark_succeeded=spark_succeeded,
        jenkins_env_vars=jenkins_env_var_values,
        tests=test_reports_by_descriptor
        )

    if historical_report_path:
        full_report_paths = [historical_report_path]
        if save_to_build_dir:
            full_report_paths.append(os.path.join(global_conf.build_root, 'full_build_report.json'))

        save_json_to_paths('full build report', report, full_report_paths, should_gzip=True)

    if save_to_build_dir:
        del report['tests']
        short_report_path = os.path.join(global_conf.build_root, 'short_build_report.json')
        save_json_to_paths('short build report', report, [short_report_path], should_gzip=False)


def is_one_shot_test(rel_binary_path):
    if rel_binary_path in ONE_SHOT_TESTS:
        return True
    for non_gtest_test in ONE_SHOT_TESTS:
        if rel_binary_path.endswith('/' + non_gtest_test):
            return True
    return False


def collect_cpp_tests(max_tests, cpp_test_program_filter, cpp_test_program_re_str):
    """
    Collect C++ test programs to run.
    @param max_tests: maximum number of tests to run. Used in debugging.
    @param cpp_test_program_filter: a collection of C++ test program names to be used as a filter
    @param cpp_test_program_re_str: a regular expression string to be used as a filter for the set
                                    of C++ test programs.
    """

    global_conf = yb_dist_tests.global_conf
    logging.info("Collecting the list of C++ test programs")
    start_time_sec = time.time()
    build_root_realpath = os.path.realpath(global_conf.build_root)
    ctest_cmd_result = command_util.run_program(
            ['/bin/bash',
             '-c',
             'cd "{}" && YB_LIST_CTEST_TESTS_ONLY=1 ctest -j8 --verbose'.format(
                 build_root_realpath)])
    test_programs = []

    for line in ctest_cmd_result.stdout.split("\n"):
        re_match = CTEST_TEST_PROGRAM_RE.match(line)
        if re_match:
            ctest_test_program = re_match.group(1)
            if ctest_test_program.startswith('/'):
                ctest_test_program = os.path.realpath(ctest_test_program)
            rel_ctest_prog_path = os.path.relpath(
                    os.path.realpath(ctest_test_program),
                    build_root_realpath)
            if rel_ctest_prog_path.startswith('../'):
                raise ValueError(
                    "Relative path to a ctest test binary ended up starting with '../', something "
                    "must be wrong: %s" % rel_ctest_prog_path)
            test_programs.append(rel_ctest_prog_path)

    test_programs = sorted(set(test_programs))
    elapsed_time_sec = time.time() - start_time_sec
    logging.info("Collected %d test programs in %.2f sec" % (
        len(test_programs), elapsed_time_sec))

    if cpp_test_program_re_str:
        cpp_test_program_re = re.compile(cpp_test_program_re_str)
        test_programs = [test_program for test_program in test_programs
                         if cpp_test_program_re.search(test_program)]
        logging.info("Filtered down to %d test programs using regular expression '%s'" %
                     (len(test_programs), cpp_test_program_re_str))

    if cpp_test_program_filter:
        cpp_test_program_filter = set(cpp_test_program_filter)
        unfiltered_test_programs = test_programs

        # test_program contains test paths relative to the root directory (including directory
        # names), and cpp_test_program_filter contains basenames only.
        test_programs = sorted(set([
                test_program for test_program in test_programs
                if os.path.basename(test_program) in cpp_test_program_filter
            ]))

        logging.info("Filtered down to %d test programs using the list from test conf file" %
                     len(test_programs))
        if unfiltered_test_programs and not test_programs:
            # This means we've filtered the list of C++ test programs down to an empty set.
            logging.info(
                    ("NO MATCHING C++ TEST PROGRAMS FOUND! Test programs from conf file: {}, "
                     "collected from ctest before filtering: {}").format(
                         set_to_comma_sep_str(cpp_test_program_filter),
                         set_to_comma_sep_str(unfiltered_test_programs)))

    if max_tests and len(test_programs) > max_tests:
        logging.info("Randomly selecting {} test programs out of {} possible".format(
                max_tests, len(test_programs)))
        random.shuffle(test_programs)
        test_programs = test_programs[:max_tests]

    if not test_programs:
        logging.info("Found no test programs")
        return []

    fine_granularity_gtest_programs = []
    one_shot_test_programs = []
    for test_program in test_programs:
        if is_one_shot_test(test_program):
            one_shot_test_programs.append(test_program)
        else:
            fine_granularity_gtest_programs.append(test_program)

    logging.info(("Found {} gtest test programs where tests will be run separately, "
                  "{} test programs to be run on one shot").format(
                    len(fine_granularity_gtest_programs),
                    len(one_shot_test_programs)))

    test_programs = fine_granularity_gtest_programs
    logging.info(
        "Collecting gtest tests for {} test programs where tests will be run separately".format(
            len(test_programs)))

    start_time_sec = time.time()

    all_test_programs = fine_granularity_gtest_programs + one_shot_test_programs
    if len(all_test_programs) <= 5:
        app_name_details = ['test programs: [{}]'.format(', '.join(all_test_programs))]
    else:
        app_name_details = ['{} test programs'.format(len(all_test_programs))]

    init_spark_context(app_name_details)
    set_global_conf_for_spark_jobs()

    # Use fewer "slices" (tasks) than there are test programs, in hope to get some batching.
    num_slices = (len(test_programs) + 1) / 2
    all_test_descriptor_lists = run_spark_action(
        lambda: spark_context.parallelize(
            test_programs, numSlices=num_slices).map(parallel_list_test_descriptors).collect()
    )
    elapsed_time_sec = time.time() - start_time_sec
    test_descriptor_strs = one_shot_test_programs + [
        test_descriptor_str
        for test_descriptor_str_list in all_test_descriptor_lists
        for test_descriptor_str in test_descriptor_str_list]
    logging.info("Collected the list of %d gtest tests in %.2f sec" % (
        len(test_descriptor_strs), elapsed_time_sec))

    return [yb_dist_tests.TestDescriptor(s) for s in test_descriptor_strs]


def is_writable(dir_path):
    return os.access(dir_path, os.W_OK)


def is_parent_dir_writable(file_path):
    return is_writable(os.path.dirname(file_path))


def fatal_error(msg):
    logging.error("Fatal: " + msg)
    raise RuntimeError(msg)


def collect_java_tests():
    java_test_list_path = os.path.join(yb_dist_tests.global_conf.build_root, 'java_test_list.txt')
    if not os.path.exists(java_test_list_path):
        raise IOError("Java test list not found at '%s'", java_test_list_path)
    with open(java_test_list_path) as java_test_list_file:
        java_test_descriptors = [
            yb_dist_tests.TestDescriptor(java_test_str.strip())
            for java_test_str in java_test_list_file.read().split("\n")
            if java_test_str.strip()
        ]
    if not java_test_descriptors:
        raise RuntimeError("Could not find any Java tests listed in '%s'" % java_test_list_path)
    return java_test_descriptors


def collect_tests(args):
    if args.cpp_test_program_regexp and args.test_conf:
        raise RuntimeException(
            "--cpp_test_program_regexp and --test_conf cannot both be specified at the same time.")

    test_conf = {}
    if args.test_conf:
        with open(args.test_conf) as test_conf_file:
            test_conf = json.load(test_conf_file)
        if args.run_cpp_tests and not test_conf['run_cpp_tests']:
            logging.info("The test configuration file says that C++ tests should be skipped")
            args.run_cpp_tests = False
        if not test_conf['run_java_tests']:
            logging.info("The test configuration file says that Java tests should be skipped")
            args.run_java_tests = False

    cpp_test_descriptors = []
    if args.run_cpp_tests:
        cpp_test_programs = test_conf.get('cpp_test_programs')
        if args.cpp_test_program_regexp and cpp_test_programs:
            logging.warning(
                    ("Ignoring the C++ test program regular expression specified on the "
                     "command line: {}").format(args.cpp_test_program_regexp))

        cpp_test_descriptors = collect_cpp_tests(
                args.max_tests,
                cpp_test_programs,
                args.cpp_test_program_regexp)

    java_test_descriptors = []
    if args.run_java_tests:
        java_test_descriptors = collect_java_tests()
        logging.info("Found %d Java tests", len(java_test_descriptors))
    return sorted(java_test_descriptors) + sorted(cpp_test_descriptors)


def load_test_list(test_list_path):
    test_descriptors = []
    with open(test_list_path, 'r') as input_file:
        for line in input_file:
            line = line.strip()
            if line:
                test_descriptors.append(yb_dist_tests.TestDescriptor())
    return test_descriptors


def propagate_env_vars():
    num_propagated = 0
    for env_var_name in JENKINS_ENV_VARS:
        if env_var_name in os.environ:
            propagated_env_vars[env_var_name] = os.environ[env_var_name]
            num_propagated += 1

    for env_var_name, env_var_value in os.environ.iteritems():
        if env_var_name.startswith(PROPAGATED_ENV_VAR_PREFIX):
            propagated_env_vars[env_var_name] = env_var_value
            logging.info("Propagating env var %s (value: %s) to Spark workers",
                         env_var_name, env_var_value)
            num_propagated += 1
    logging.info("Number of propagated environment variables: %s", num_propagated)


def run_spark_action(action):
    import py4j
    try:
        results = action()
    except py4j.protocol.Py4JJavaError:
        logging.error("Spark job failed to run! Jenkins should probably restart this build.")
        raise

    return results


def main():
    parser = argparse.ArgumentParser(
        description='Run tests on Spark.')
    parser.add_argument('--verbose', action='store_true',
                        help='Enable debug output')
    parser.add_argument('--java', dest='run_java_tests', action='store_true',
                        help='Run Java tests')
    parser.add_argument('--cpp', dest='run_cpp_tests', action='store_true',
                        help='Run C++ tests')
    parser.add_argument('--all', dest='run_all_tests', action='store_true',
                        help='Run tests in all languages')
    parser.add_argument('--test_list',
                        help='A file with a list of tests to run. Useful when e.g. re-running '
                             'failed tests using a file produced with --failed_test_list.')
    parser.add_argument('--build-root', dest='build_root', required=True,
                        help='Build root (e.g. ~/code/yugabyte/build/debug-gcc-dynamic-community)')
    parser.add_argument('--max-tests', type=int, dest='max_tests',
                        help='Maximum number of tests to run. Useful when debugging this script '
                             'for faster iteration. This number of tests will be randomly chosen '
                             'from the test suite.')
    parser.add_argument('--sleep_after_tests', action='store_true',
                        help='Sleep for a while after test are done before destroying '
                             'SparkContext. This allows to examine the Spark app UI.')
    parser.add_argument('--reports-dir', dest='report_base_dir',
                        help='A parent directory for storing build reports (such as per-test '
                             'run times and whether the Spark job succeeded.)')
    parser.add_argument('--write_report', action='store_true',
                        help='Actually enable writing build reports. If this is not '
                             'specified, we will only read previous test reports to sort tests '
                             'better.')
    parser.add_argument('--save_report_to_build_dir', action='store_true',
                        help='Save a test report to the build directory directly, in addition '
                             'to any reports saved in the common reports directory. This should '
                             'work even if neither --reports-dir or --write_report are specified.')
    parser.add_argument('--cpp_test_program_regexp',
                        help='A regular expression to filter C++ test program names on.')
    parser.add_argument('--test_conf',
                        help='A file with a JSON configuration describing what tests to run, '
                             'produced by dependency_graph.py')
    parser.add_argument('--num_repetitions', type=int, default=1,
                        help='Number of times to run each test.')
    parser.add_argument('--failed_test_list',
                        help='A file path to save the list of failed tests to. The format is '
                             'one test descriptor per line.')
    parser.add_argument('--allow_no_tests', action='store_true',
                        help='Allow running with filters that yield no tests to run. Useful when '
                             'debugging.')
    parser.add_argument('--spark-master-url',
                        help='Override Spark master URL to use. Useful for debugging.')

    args = parser.parse_args()
    global g_spark_master_url_override
    g_spark_master_url_override = args.spark_master_url

    # ---------------------------------------------------------------------------------------------
    # Argument validation.

    if args.run_all_tests:
        args.run_java_tests = True
        args.run_cpp_tests = True

    global verbose
    verbose = args.verbose

    log_level = logging.INFO
    logging.basicConfig(
        level=log_level,
        format="[%(filename)s:%(lineno)d] %(asctime)s %(levelname)s: %(message)s")

    if not args.run_cpp_tests and not args.run_java_tests:
        fatal_error("At least one of --java or --cpp has to be specified")

    yb_dist_tests.set_global_conf_from_args(args)

    report_base_dir = args.report_base_dir
    write_report = args.write_report
    if report_base_dir and not os.path.isdir(report_base_dir):
        fatal_error("Report base directory '{}' does not exist".format(report_base_dir))

    if write_report and not report_base_dir:
        fatal_error("--write_report specified but the reports directory (--reports-dir) is not")

    if write_report and not is_writable(report_base_dir):
        fatal_error(
            "--write_report specified but the reports directory ('{}') is not writable".format(
                report_base_dir))

    if args.num_repetitions < 1:
        fatal_error("--num_repetitions must be at least 1, got: {}".format(args.num_repetitions))

    failed_test_list_path = args.failed_test_list
    if failed_test_list_path and not is_parent_dir_writable(failed_test_list_path):
        fatal_error(("Parent directory of failed test list destination path ('{}') is not " +
                     "writable").format(args.failed_test_list))

    test_list_path = args.test_list
    if test_list_path and not os.path.isfile(test_list_path):
        fatal_error("File specified by --test_list does not exist or is not a file: '{}'".format(
            test_list_path))

    # ---------------------------------------------------------------------------------------------
    # Start the timer.
    global_start_time = time.time()

    if test_list_path:
        test_descriptors = load_test_list(test_list_path)
    else:
        test_descriptors = collect_tests(args)

    if not test_descriptors and not args.allow_no_tests:
        logging.info("No tests to run")
        return

    propagate_env_vars()

    num_tests = len(test_descriptors)

    if args.max_tests and num_tests > args.max_tests:
        logging.info("Randomly selecting {} tests out of {} possible".format(
                args.max_tests, num_tests))
        random.shuffle(test_descriptors)
        test_descriptors = test_descriptors[:args.max_tests]
        num_tests = len(test_descriptors)

    if args.verbose:
        for test_descriptor in test_descriptors:
            logging.info("Will run test: {}".format(test_descriptor))

    num_repetitions = args.num_repetitions
    total_num_tests = num_tests * num_repetitions
    logging.info("Running {} tests on Spark, {} times each, for a total of {} tests".format(
        num_tests, num_repetitions, total_num_tests))

    if num_repetitions > 1:
        test_descriptors = [
            test_descriptor.with_attempt_index(i)
            for test_descriptor in test_descriptors
            for i in xrange(1, num_repetitions + 1)
        ]

    app_name_details = ['{} tests total'.format(total_num_tests)]
    if num_repetitions > 1:
        app_name_details += ['{} repetitions of {} tests'.format(num_repetitions, num_tests)]
    init_spark_context(app_name_details)

    set_global_conf_for_spark_jobs()

    # By this point, test_descriptors have been duplicated the necessary number of times, with
    # attempt indexes attached to each test descriptor.
    spark_succeeded = False
    if test_descriptors:
        logging.info("Running {} tasks on Spark".format(total_num_tests))
        assert total_num_tests == len(test_descriptors), \
            "total_num_tests={}, len(test_descriptors)={}".format(
                    total_num_tests, len(test_descriptors))

        # Randomize test order to avoid any kind of skew.
        random.shuffle(test_descriptors)

        test_names_rdd = spark_context.parallelize(
                [test_descriptor.descriptor_str for test_descriptor in test_descriptors],
                numSlices=total_num_tests)

        results = run_spark_action(lambda: test_names_rdd.map(parallel_run_test).collect())
    else:
        # Allow running zero tests, for testing the reporting logic.
        results = []

    test_exit_codes = set([result.exit_code for result in results])

    global_exit_code = 0 if test_exit_codes == set([0]) else 1

    logging.info("Tests are done, set of exit codes: {}, will return exit code {}".format(
        sorted(test_exit_codes), global_exit_code))
    failures_by_language = defaultdict(int)
    failed_test_desc_strs = []
    for result in results:
        if result.exit_code != 0:
            how_test_failed = ""
            if result.failed_without_output:
                how_test_failed = " without any output"
            logging.info("Test failed{}: {}".format(how_test_failed, result.test_descriptor))
            failures_by_language[result.test_descriptor.language] += 1
            failed_test_desc_strs.append(result.test_descriptor.descriptor_str)

    if failed_test_list_path:
        logging.info("Writing the list of failed tests to '{}'".format(failed_test_list_path))
        with open(failed_test_list_path, 'w') as failed_test_file:
            failed_test_file.write("\n".join(failed_test_desc_strs) + "\n")

    for language, num_failures in failures_by_language.iteritems():
        logging.info("Failures in {} tests: {}".format(language, num_failures))

    total_elapsed_time_sec = time.time() - global_start_time
    logging.info("Total elapsed time: {} sec".format(total_elapsed_time_sec))
    if report_base_dir and write_report or args.save_report_to_build_dir:
        save_report(report_base_dir, results, total_elapsed_time_sec, spark_succeeded,
                    save_to_build_dir=args.save_report_to_build_dir)

    if args.sleep_after_tests:
        # This can be used as a way to keep the Spark app running during debugging while examining
        # its UI.
        time.sleep(600)

    sys.exit(global_exit_code)


if __name__ == '__main__':
    main()
