# A scripted host for spectate-guest.sh and spectate-leave.sh: its V in a
# scripted moment beside the guest. A tier 2 walk and talk (played from the
# guest's own V), a tier 5 cutscene in it (watched: reason a), the walk and
# talk again (played, the moment going on), a dive into cyberspace (watched:
# reason b), back, then the moment ends. Format: peer/src/main.cpp (moment,
# camera, world).
3 moment on 2
3 camera on 70
10 moment on watch 5
13 camera cut
17 moment on 2
24 world cyberspace
24 moment on watch 1
30 world city
30 moment on 2
34 moment off
34 camera off
