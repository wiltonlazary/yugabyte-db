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
package org.yb.minicluster;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import org.yb.util.EnvAndSysPropertyUtil;

/**
 * Helper runnable that can log what the processes are sending on their stdout and stderr that
 * we'd otherwise miss.
 */
public class LogPrinter {

  private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  private final String logPrefix;
  private final InputStream stream;
  private final Thread thread;
  private final AtomicBoolean stopRequested = new AtomicBoolean(false);
  private final Object stopper = new Object();

  private static final AtomicLong totalLoggedSize = new AtomicLong();
  private static final AtomicBoolean logSizeExceededThrown = new AtomicBoolean(false);

  private boolean stopped = false;
  private String errorMessage;

  private static final boolean LOG_PRINTER_DEBUG = false;

  private static final long MAX_ALLOWED_LOGGED_BYTES =
      EnvAndSysPropertyUtil.getLongEnvVarOrSystemProperty(
          "YB_JAVA_TEST_MAX_ALLOWED_LOG_BYTES",
          512 * 1024 * 1024);

  // A mechanism to wait for a line in the log that says that the server is starting.
  private LogErrorListener errorListener;

  public LogPrinter(InputStream stream, String logPrefix) {
    this(stream, logPrefix, null);
  }

  public LogPrinter(InputStream stream, String logPrefix, LogErrorListener errorListener) {
    this.stream = stream;

    this.logPrefix = logPrefix;
    this.thread = new Thread(() -> runThread());
    this.errorListener = errorListener;
    if (errorListener != null) {
      errorListener.associateWithLogPrinter(this);
    }

    thread.setDaemon(true);
    thread.setName("Log printer for " + logPrefix.trim());
    thread.start();
  }

  private void runThread() {
    try {
      String line;
      BufferedReader in = new BufferedReader(new InputStreamReader(stream));
      try {
        if (LOG_PRINTER_DEBUG) {
          LOG.info("Starting log printer with prefix '" + logPrefix +
                   "', total log size limit: " + MAX_ALLOWED_LOGGED_BYTES +
                   ", used log size: " + totalLoggedSize.get());
        }
        try {
          while (!stopRequested.get()) {
            while ((line = in.readLine()) != null) {
              if (errorListener != null) {
                errorListener.handleLine(line);
              }
              System.out.println(logPrefix + line);
              if (totalLoggedSize.addAndGet(line.length() + 1) > MAX_ALLOWED_LOGGED_BYTES) {
                if (errorMessage == null) {
                  errorMessage = "Max total log size exceeded: " + MAX_ALLOWED_LOGGED_BYTES;
                  // Show the error once per LogPrinter instance.
                  LOG.warn(errorMessage + " in log printer with prefix " + logPrefix);
                }

                if (logSizeExceededThrown.compareAndSet(false, true)) {
                  LOG.warn(errorMessage);
                  throw new AssertionError(errorMessage);
                }
                return;
              }
              System.out.flush();
            }
            // Sleep for a short time and give the child process a chance to generate more output.
            Thread.sleep(10);
          }
        } catch (InterruptedException iex) {
          // This probably means we're stopping, OK to ignore.
        }
        if (LOG_PRINTER_DEBUG) {
          LOG.info("Finished log printer with prefix " + logPrefix);
        }
      } finally {
        if (LOG_PRINTER_DEBUG) {
          LOG.info("Closing input stream for log printer with prefix " + logPrefix);
        }

        in.close();
      }
    } catch (Exception e) {
      String msg = e.getMessage();
      if (msg == null || !msg.contains("Stream closed")) {
        LOG.error("Caught error while reading a process's output", e);
      }
    } finally {
      if (LOG_PRINTER_DEBUG) {
        LOG.info("Closing process output stream with prefix " + logPrefix);
      }

      try {
        stream.close();
      } catch (IOException ex) {
        // Ignore, we're stopping anyway.
      }
      synchronized (stopper) {
        stopped = true;
        stopper.notifyAll();
      }
    }
  }

  public void stop() throws InterruptedException {
    stopRequested.set(true);
    thread.interrupt();
    synchronized (stopper) {
      while (!stopped) {
        stopper.wait();
      }
    }
    if (errorListener != null) {
      errorListener.reportErrorsAtEnd();
    }
  }

  public String getError() {
    return errorMessage;
  }
}
