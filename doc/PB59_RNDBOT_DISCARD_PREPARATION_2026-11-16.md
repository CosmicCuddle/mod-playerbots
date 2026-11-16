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
