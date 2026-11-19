# A player drives a car round V, gets out and walks: copy, seat, parked
# shellcheck shell=bash disable=SC2317,SC2154
ensure host
# A real drive on real roads, then out of the car (recordings/play/drive.tplay),
# else circles round V.
play drive --mode drive --body female --drive-seconds 15
# The recorded V walks to its car first (drive.tplay: in at 51 s, out at
# 86 s, in again at 99 s): the copy comes when the recording says so.
if [ "$played" = 1 ]; then expect 'copy spawning' 120; else expect 'copy spawning' 30; fi
expect 'seated in seat_front_left' 30
if [ "$played" = 1 ]; then expect 'on foot' 600; else expect 'on foot' 40; fi
expect 'car left parked' 10
expect 'movement controller back' 10
# The copy its driver sat in can be deleted (it crashed while our Mount
# released the car once per mount: engine-knowledge §13.6).
cmd vehicle clear
expect 'vehicle clear: done' 10
pause 8
reject 'watchdog: game thread silent'
