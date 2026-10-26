#include "LocalWssSessionPolicy.h"
#include <cassert>
#include <cstdint>

int main() {
  using local_wss_session_policy::auth_expired;
  assert(!auth_expired(-1, false, 100, 200, 100));
  assert(!auth_expired(54, true, 100, 200, 100));
  assert(!auth_expired(54, false, 100, 199, 100));
  assert(auth_expired(54, false, 100, 200, 100));
  assert(auth_expired(54, false, UINT32_MAX - 20, 80, 100));
  using local_wss_session_policy::session_gone;
  assert(!session_gone(true, true));
  assert(!session_gone(false, false));
  assert(session_gone(true, false));
  local_wss_session_policy::UnclaimedWatch watches[2]{};
  const int lone[] = {54};
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 1, -1,
                                                    100, 1000) == -1);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 1, -1,
                                                    1099, 1000) == -1);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 1, -1,
                                                    1100, 1000) == 54);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 1, -1,
                                                    1200, 1000) == -1);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 1, 54,
                                                    1300, 1000) == -1);
  assert(watches[0].fd == -1);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 1, -1,
                                                    UINT32_MAX - 20, 100) == -1);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 1, -1,
                                                    80, 100) == 54);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, lone, 0, -1,
                                                    100, 100) == -1);
  const int paired[] = {54, 55};
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, paired, 2, 55,
                                                    200, 100) == -1);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, paired, 2, 55,
                                                    301, 100) == 54);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, paired, 2, 55,
                                                    350, 100) == -1);
  assert(local_wss_session_policy::expired_unclaimed(watches, 2, paired + 1, 1, 55,
                                                    400, 100) == -1);
  assert(watches[0].fd == -1);
}
