/**
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 */
package org.yb.util;

import org.apache.commons.lang3.math.NumberUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.minicluster.LogPrinter;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class CoreFileUtil {

  public static final boolean IS_MAC =
      System.getProperty("os.name").toLowerCase().startsWith("mac os x");

  private CoreFileUtil() {
  }

  private static final Logger LOG = LoggerFactory.getLogger(CoreFileUtil.class);

  public static final int NO_PID = -1;

  public enum CoreFileMatchMode {
    /**
     * Has to be a core file ending with this exact pid.
     */
    EXACT_PID {
      @Override
      boolean allowNoPid() { return false; }
    },

    /**
     * Either "core" with no PID suffix, or this exact PID.
     */
    NO_PID_OR_EXACT_PID,

    /**
     * Any core file within the given directory, with or without a PID in the file name.
     */
    ANY_CORE_FILE;

    boolean allowNoPid() { return true; }
  }

  public static void processCoreFile(int pid,
                                     String executablePath,
                                     String programDescription,
                                     File coreFileDir,
                                     CoreFileMatchMode matchMode) throws Exception {
    List<String> coreFileBasenames = new ArrayList<>();

    if (coreFileDir == null) {
      if (IS_MAC) {
        coreFileDir = new File("/cores");
      } else {
        coreFileDir = new File(System.getProperty("user.dir"));
      }
    }

    if (matchMode == CoreFileMatchMode.ANY_CORE_FILE) {
      for (File fileInDir : coreFileDir.listFiles()) {
        if (fileInDir.isFile()) {
          String name = fileInDir.getName();
          if (name.equals("core") ||
              name.startsWith("core.") && NumberUtils.isDigits(name.substring(5))) {
            coreFileBasenames.add(fileInDir.getName());
          }
        }
      }
    } else {
      if (pid != NO_PID) {
        coreFileBasenames.add("core." + pid);
      }
      if (matchMode.allowNoPid()) {
        coreFileBasenames.add("core");
      }
    }

    for (String coreBasename : coreFileBasenames) {
      final File coreFile = new File(coreFileDir, coreBasename);
      if (coreFile.exists()) {
        LOG.warn("Found core file '{}' from {}", coreFile.getAbsolutePath(), programDescription);
        String analysisScript = TestUtils.findYbRootDir() + "/build-support/analyze_core_file.sh";
        List<String> analysisArgs = Arrays.asList(
            analysisScript,
            "--core",
            coreFile.getAbsolutePath(),
            "--executable",
            executablePath
        );
        LOG.warn("Analyzing core file using the command: " + analysisArgs);
        ProcessBuilder procBuilder = new ProcessBuilder().command(analysisArgs);
        procBuilder.redirectErrorStream(true);
        Process analysisProcess = procBuilder.start();

        LogPrinter logPrinter = new LogPrinter(analysisProcess.getInputStream(), "    ");
        analysisProcess.waitFor();
        logPrinter.stop();

        if (analysisProcess.exitValue() != 0) {
          LOG.warn("Core file analysis script " + analysisProcess + " exited with code: " +
              analysisProcess.exitValue());
        } else {
          if (coreFile.delete()) {
            LOG.warn("Deleted core file at '{}'", coreFile.getAbsolutePath());
          } else {
            LOG.warn("Failed to delete core file at '{}'", coreFile.getAbsolutePath());
          }
        }
      }
    }
  }

}
