# PB59 native replacement preparation

Core paired source: 6cc2edbc351883bbf17e4bbec177575f10efd620 (fixture child 8d6ac439eb8a42d028de2b30cb298c24552ba873). This fixture compiles the actual Playerbots batch and original-discard headers with a mock Core item factory and Player. No inventory mutation or server/DB.

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Itests/pb59_native_replacement_mock -Isrc/Bot -I"$CORE/src/server/game/Entities/Player" tests/pb59_native_replacement_preparation.cpp -o /tmp/pb59-replacement
/tmp/pb59-replacement
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/pb59_native_replacement_mock -Isrc/Bot -I"$CORE/src/server/game/Entities/Player" tests/pb59_native_replacement_preparation.cpp -o /tmp/pb59-replacement-san
/tmp/pb59-replacement-san
```

Both passed 42 checks on exact final header blob a41de8f819b4302eb1234d17ea7231bb960c5ce2. Core separately tests the extracted actual detached factory (45 checks per configuration). Batch checks include exact signed properties, allocation failure, full required mask, cap/quality, distinct GUIDs including collision with original gear, callback refusal and original/session changes. Actual full native compile, owner-aware creation notifications, final equip legality, custody, equipment effects, disposal and gameplay remain pending. The command path is still disabled and has no authoritative native candidate source yet.
