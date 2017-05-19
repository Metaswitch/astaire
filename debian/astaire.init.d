#! /bin/sh

# @file astaire.init.d
#
# Copyright (C) Metaswitch Networks
# If license terms are provided to you in a COPYING file in the root directory
# of the source code repository by which you are accessing this code, then
# the license outlined in that COPYING file applies to your use.
# Otherwise no rights are granted except for those provided to you by
# Metaswitch Networks in a separate written agreement.

### BEGIN INIT INFO
# Provides:          astaire
# Required-Start:    $remote_fs $syslog
# Required-Stop:     $remote_fs $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Astaire
# Description:       Astaire, active resynchronisation for memcached clusters
### END INIT INFO

# Author: Project Clearwater Maintainers <maintainers@projectclearwater.org>
#
# Please remove the "Author" lines above and replace them
# with your own name if you copy and modify this script.

# Do NOT "set -e"

# PATH should only include /usr/* if it runs after the mountnfs.sh script
PATH=/sbin:/usr/sbin:/bin:/usr/bin
DESC="Astaire Active Resynchronization Daemon"
NAME=astaire
EXECNAME=astaire
PIDFILE=/var/run/$NAME/$NAME.pid
DAEMON=/usr/share/clearwater/bin/astaire
HOME=/etc/clearwater
log_directory=/var/log/$NAME

# Exit if the package is not installed
[ -x "$DAEMON" ] || exit 0

# Read configuration variable file if it is present
#[ -r /etc/default/$NAME ] && . /etc/default/$NAME

# Load the VERBOSE setting and other rcS variables
. /lib/init/vars.sh

# Define LSB log_* functions.
# Depend on lsb-base (>= 3.2-14) to ensure that this file is present
# and status_of_proc is working.
. /lib/lsb/init-functions

#
# Function to pull in settings prior to starting the daemon
#
get_settings()
{
        # Set up defaults and then pull in the settings for this node.
        . /etc/clearwater/config

        # Set up defaults for user settings then pull in any overrides.
        log_level=2
        [ -r /etc/clearwater/user_settings ] && . /etc/clearwater/user_settings
        [ -z "$signaling_namespace" ] || namespace_prefix="ip netns exec $signaling_namespace"
}

#
# Function that starts the daemon/service
#
do_start()
{
        # Return
        #   0 if daemon has been started
        #   1 if daemon was already running
        #   2 if daemon could not be started

        # Allow us to write to the pidfile directory
        install -m 755 -o $NAME -g root -d /var/run/$NAME && chown -R $NAME /var/run/$NAME

        start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --test > /dev/null \
                || return 1

        # daemon is not running, so attempt to start it.
        export LD_LIBRARY_PATH=/usr/share/clearwater/astaire/lib
        ulimit -Hn 1000000
        ulimit -Sn 1000000
        ulimit -c unlimited
        # enable gdb to dump a parent astaire process's stack
        echo 0 > /proc/sys/kernel/yama/ptrace_scope
        get_settings
        DAEMON_ARGS="--local-name=$local_ip:11211
                     --cluster-settings-file=/etc/clearwater/cluster_settings
                     --log-file=$log_directory
                     --log-level=$log_level"

        $namespace_prefix start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --chuid $NAME --chdir $HOME --nicelevel 10 -- $DAEMON_ARGS --daemon --pidfile=$PIDFILE \
                || return 2
        # Add code here, if necessary, that waits for the process to be ready
        # to handle requests from services started subsequently which depend
        # on this one.  As a last resort, sleep for some time.
}

#
# Function that runs the daemon/service in the foreground
#
do_run()
{
        # Allow us to write to the pidfile directory
        install -m 755 -o $NAME -g root -d /var/run/$NAME && chown -R $NAME /var/run/$NAME

        export LD_LIBRARY_PATH=/usr/share/clearwater/astaire/lib
        ulimit -Hn 1000000
        ulimit -Sn 1000000
        ulimit -c unlimited
        # enable gdb to dump a parent astaire process's stack
        echo 0 > /proc/sys/kernel/yama/ptrace_scope
        get_settings
        DAEMON_ARGS="--local-name=$local_ip:11211
                     --cluster-settings-file=/etc/clearwater/cluster_settings
                     --log-file=$log_directory
                     --log-level=$log_level"

        $namespace_prefix start-stop-daemon --start --quiet --pidfile $PIDFILE --exec $DAEMON --chuid $NAME --chdir $HOME --nicelevel 10 -- $DAEMON_ARGS --pidfile=$PIDFILE \
                || return 2
        # Add code here, if necessary, that waits for the process to be ready
        # to handle requests from services started subsequently which depend
        # on this one.  As a last resort, sleep for some time.
}

#
# Function that stops the daemon/service
#
do_stop()
{
        # Return
        #   0 if daemon has been stopped
        #   1 if daemon was already stopped
        #   2 if daemon could not be stopped
        #   other if a failure occurred
        start-stop-daemon --stop --quiet --retry=TERM/30/KILL/5 --pidfile $PIDFILE --name $EXECNAME
        RETVAL="$?"
        return "$RETVAL"
}

#
# Function that aborts the daemon/service
#
# This is very similar to do_stop except it sends SIGABRT to dump a core file
# and waits longer for it to complete.
#
do_abort()
{
        # Return
        #   0 if daemon has been stopped
        #   1 if daemon was already stopped
        #   2 if daemon could not be stopped
        #   other if a failure occurred
        start-stop-daemon --stop --quiet --retry=ABRT/60/KILL/5 --pidfile $PIDFILE --name $EXECNAME
        RETVAL="$?"
        # If the abort failed, it may be because the PID in PIDFILE doesn't match the right process
        # In this window condition, we may not recover, so remove the PIDFILE to get it running
        if [ $RETVAL != 0 ]; then
          rm -f $PIDFILE
        fi
        return "$RETVAL"
}

#
# Function that sends a SIGHUP to the daemon/service
#
do_reload() {
        #
        # If the daemon can reload its configuration without
        # restarting (for example, when it is sent a SIGHUP),
        # then implement that here.
        #
        start-stop-daemon --stop --signal HUP --quiet --pidfile $PIDFILE --name $EXECNAME
        return 0
}

#
# Polls astaire until resynchronization completes
#
do_wait_sync() {
        # Wait for 2s to give Astaire a chance to have updated its statistics.
        sleep 2

        # Query astaire via the 0MQ socket, parse out the number of buckets
        # needing resync and check if it's 0.  If not, wait for 5s and try again.
        num_cycles_unchanged=0
        while true
        do
                # Retrieve the statistics. Check that we got the statistics from Astaire -
                # if we didn't then Astaire probably isn't running, and there's no use
                # waiting for it to sync.
                stats=$(/usr/share/clearwater/astaire/bin/cw_stat astaire astaire_global)
                if [ $? != 0 ]
                then
                  logger astaire: Wait sync aborting as unable to get statistics from Astaire
                  break
                fi

                parsed_stats=$(echo "$stats" |
                               egrep '(buckets(NeedingResync|Resynchronized)|entriesResynchronized)' |
                               cut -d: -f2)

                bucket_need_resync=`echo $parsed_stats | cut -d\  -f1`
                bucket_resynchronized=`echo $parsed_stats | cut -d\  -f2`
                entry_resynchronized=`echo $parsed_stats | cut -d\  -f3`

                # If the number of buckets needing resync is 0, we're finished
                if [ "$bucket_need_resync" = "0" ]
                then
                  break
                fi

                # If the number of buckets needing resync hasn't changed for the last 120 cycles (i.e.
                # over the last 10 minutes), make a syslog and stop waiting.  We don't
                # expect resyncs to take more than 10 minutes in normal operation, so this
                # suggests that local service has failed  We need to
                # end the wait in this case as the potential service impact of aborting
                # the wait early is far outweighed by the impact on management operations
                # of an infinite wait.
                if [ "$bucket_need_resync" = "$last_bucket_need_resync" ]
                then
                  num_cycles_unchanged=$(( $num_cycles_unchanged + 1 ))

                  if [ $num_cycles_unchanged -ge 120 ]
                  then
                    logger astaire: Wait sync aborting as unsynced bucket count apparently stuck at $bucket_need_resync
                    break
                  fi

                else
                  last_bucket_need_resync=$bucket_need_resync
                  num_cycles_unchanged=0
                fi

                # If we have numeric statistics, display them.
                if [ "$bucket_need_resync" != "" ] &&
                   [ "$bucket_resynchronized" != "" ] &&
                   [ "$(echo $bucket_need_resync$bucket_resynchronized$entry_resynchronized | tr -d 0-9)" = "" ]
                then
                       echo -n "($entry_resynchronized - $bucket_resynchronized/$bucket_need_resync)"
                fi

                # Indicate that we're still waiting and sleep for 5s
                echo -n ...
                sleep 5
        done
        return 0
}

do_full_resync() {
        # Send Astaire SIGUSR1 to make it do a full resync.
        start-stop-daemon --stop --signal USR1 --quiet --pidfile $PIDFILE --name $EXECNAME
        return 0
}

# There should only be at most one astaire process, and it should be the one in /var/run/astaire.pid.
# Sanity check this, and kill and log any leaked ones.
if [ -f $PIDFILE ] ; then
  leaked_pids=$(pgrep -f "^$DAEMON" | grep -v $(cat $PIDFILE))
else
  leaked_pids=$(pgrep -f "^$DAEMON")
fi
if [ -n "$leaked_pids" ] ; then
  for pid in $leaked_pids ; do
    logger -p daemon.error -t $NAME Found leaked astaire $pid \(correct is $(cat $PIDFILE)\) - killing $pid
    kill -9 $pid
  done
fi

case "$1" in
  start)
        [ "$VERBOSE" != no ] && log_daemon_msg "Starting $DESC" "$NAME"
        do_start
        case "$?" in
                0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
                2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
        esac
        ;;
  run)
        [ "$VERBOSE" != no ] && log_daemon_msg "Running $DESC" "$NAME"
        do_run
        case "$?" in
                0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
                2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
        esac
        ;;
  stop)
        [ "$VERBOSE" != no ] && log_daemon_msg "Stopping $DESC" "$NAME"
        do_stop
        case "$?" in
                0|1) [ "$VERBOSE" != no ] && log_end_msg 0 ;;
                2) [ "$VERBOSE" != no ] && log_end_msg 1 ;;
        esac
        ;;
  status)
       status_of_proc "$DAEMON" "$NAME" && exit 0 || exit $?
       ;;
  reload|force-reload)
        log_daemon_msg "Reloading $DESC" "$NAME"
        do_reload
        log_end_msg $?
        ;;
  restart)
        log_daemon_msg "Restarting $DESC" "$NAME"
        do_stop
        case "$?" in
          0|1)
                do_start
                case "$?" in
                        0) log_end_msg 0 ;;
                        1) log_end_msg 1 ;; # Old process is still running
                        *) log_end_msg 1 ;; # Failed to start
                esac
                ;;
          *)
                # Failed to stop
                log_end_msg 1
                ;;
        esac
        ;;
  abort)
        log_daemon_msg "Aborting $DESC" "$NAME"
        do_abort
        ;;
  abort-restart)
        log_daemon_msg "Abort-Restarting $DESC" "$NAME"
        do_abort
        case "$?" in
          0|1)
                do_start
                case "$?" in
                        0) log_end_msg 0 ;;
                        1) log_end_msg 1 ;; # Old process is still running
                        *) log_end_msg 1 ;; # Failed to start
                esac
                ;;
          *)
                # Failed to stop
                log_end_msg 1
                ;;
        esac
        ;;
  wait-sync)
        log_daemon_msg "Waiting for synchronization - $DESC" "$NAME"
        do_wait_sync
        ;;
  full-resync)
        log_daemon_msg "Forcing full resync - $DESC" "$NAME"
        do_full_resync
        ;;
  *)
        echo "Usage: $SCRIPTNAME {start|run|stop|status|restart|reload|force-reload|abort|abort-restart|wait-sync|full-resync}" >&2
        exit 3
        ;;
esac

:
