# PB59 exact native storage preparation fixture

This tests the actual preparation header with mocked Player/Bag/WorldSession and the pinned Core inventory facts. It is not a native worldserver or gameplay test.

From mod-playerbots root, with CORE set to the paired AzerothCore checkout at 28df5c5:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Itests/pb59_native_storage_mock -Isrc/Bot -I"$CORE/src/server/game/Entities/Player" tests/pb59_native_storage_preparation.cpp -o /tmp/pb59-prep-test
/tmp/pb59-prep-test
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/pb59_native_storage_mock -Isrc/Bot -I"$CORE/src/server/game/Entities/Player" tests/pb59_native_storage_preparation.cpp -o /tmp/pb59-prep-san
/tmp/pb59-prep-san
```

Expected 28 checks PASS in each. The native header is f8aeb7fc28d8f00567822fe3f67b3f76222657cb, and the tested matching, observation, transaction and Core facts headers are byte-identical to their source pins. No items move; the fixture proves only read-only reservation behavior. Actual script policy, lifetime and equipment-effects reconciliation require native build/runtime evidence. Storage NoSpace means no verified exact matching, not a proven empty item-candidate domain; never use it as authorization to strip.
