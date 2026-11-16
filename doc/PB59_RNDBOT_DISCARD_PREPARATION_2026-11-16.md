# PB59 RNDBOT disposal planning

Owner decision: coordination #59 comment 6916780239. Verified RandomBots discard old combat gear rather than accumulating it in bags. Altbots retain old equipment. Body, tabard, bags and unrelated inventory excluded.

Native preparation captures all combat positions, current owner and session incarnation, then verifies CanUnequipItem and all copied facts after callbacks. No bag reservations. The planner distinguishes StripDiscard from StripToBags and requires a complete search for any discard intent. The previous empty-domain fast path now goes through the normal solver/proof checks. The old bag-storage handoff cannot consume a discard replacement plan. Neither path performs destruction. The native collector, exact item staging, actual Core effects/custody/save-safe disposal and final verification remain required. A plan is not irreversible permission.

Run the new focused fixture against the existing mock Player and pinned Core inventory facts:
```sh
g++ -std=c++20 -Wall -Wextra -Werror -Itests/pb59_native_storage_mock -Isrc/Bot -I"$CORE/src/server/game/Entities/Player" tests/pb59_native_discard_preparation.cpp -o /tmp/pb59-discard
/tmp/pb59-discard
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/pb59_native_storage_mock -Isrc/Bot -I"$CORE/src/server/game/Entities/Player" tests/pb59_native_discard_preparation.cpp -o /tmp/pb59-discard-san
/tmp/pb59-discard-san
```
27 checks per configuration. The route fixture adds six cases; full native and full route test execution remain a separate gate. No live activation.

Outcome matrix independently run against exact outcome header blob 1b512ada2d5aab94876e84bf30066c53e4ca7953 and command outcome blob 302d301b22956580e42a8d20ab4f0cc1ad52362c:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Isrc/Bot tests/pb59_rndbot_discard_outcome.cpp -o /tmp/pb59-outcomes
/tmp/pb59-outcomes
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/Bot tests/pb59_rndbot_discard_outcome.cpp -o /tmp/pb59-outcomes-san
/tmp/pb59-outcomes-san
```

Both report 120 outcome cases PASS. Scope: pure outcome plus the separate 27-case native preparation mock; the modified complete route fixture has not been executed in this environment.
