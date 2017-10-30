/**
 * @file rogers_pd_definitions.hpp - Rogers problem determination logs
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef ROGERS_PD_DEFINITIONS__
#define ROGERS_PD_DEFINITIONS__

#include <string>
#include "pdlog.h"

// The fields for each PDLog instance contains:
//   Identity - Identifies the log id to be used in the syslog id field.
//   Severity - One of Emergency, Alert, Critical, Error, Warning, Notice,
//              and Info. Only LOG_ERROR or LOG_NOTICE are used.
//   Message  - Formatted description of the condition.
//   Cause    - The cause of the condition.
//   Effect   - The effect the condition.
//   Action   - A list of one or more actions to take to resolve the condition
//              if it is an error.

const static PDLog1<const char*> CL_ROGERS_INVALID_OPTION
(
  PDLogBase::CL_ROGERS_ID + 1,
  LOG_ERR,
  "Fatal - An invalid command line option, %s, was passed to Rogers. The application will exit and restart until the problem is fixed. Run with --help for options.",
  "There was an invalid command line option in the configuration files.",
  "The application will exit and restart until the problem is fixed.",
  "Check that the configuration files in /etc/clearwater and /etc/clearwater/cluster_settings are correct."
);

const static PDLog CL_ROGERS_STARTED
(
  PDLogBase::CL_ROGERS_ID + 2,
  LOG_INFO,
  "Rogers started.",
  "The Rogers application is starting.",
  "Normal.",
  "None."
);

const static PDLog CL_ROGERS_ENDED
(
  PDLogBase::CL_ROGERS_ID + 3,
  LOG_ERR,
  "Rogers ended - Termination signal received - terminating.",
  "Rogers has been terminated by Monit or has exited.",
  "The Rogers service is not longer available.",
    "(1). This occurs normally when Rogers is stopped. "
    "(2). If Rogers fails to respond to monit queries in a timely manner, monit restarts the application."
);

const static PDLog1<const char*> CL_ROGERS_TERMINATED
(
  PDLogBase::CL_ROGERS_ID + 4,
  LOG_ERR,
  "Fatal - Rogers has exited or been terminated with signal %s.",
  "Rogers has encountered a fatal software error or has been terminated",
  "The application will exit and restart until the problem is fixed.",
  "Ensure that Rogers has been installed correctly and that it "
    "has valid configuration."
);

#endif
