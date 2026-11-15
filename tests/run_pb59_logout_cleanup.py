#!/usr/bin/env python3
"""Compile the actual queued cleanup class against minimal native API stubs."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitizers", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / "src/Script/WorldThr/PlayerbotOperations.h").read_text()
start = source.index("class BotLogoutGroupCleanupOperation")
end = source.index("// Add player bot operation", start)
stub = r"""
#include <cassert>
#include <cstdint>
#include <string>
using uint32=std::uint32_t;
using ObjectGuid=std::uint64_t;
struct Player;
struct WorldSession { Player* player; std::uint64_t id; Player* GetPlayer(){return player;} std::uint64_t GetPB59SessionIncarnation(){return id;} };
struct Group {};
struct Player { std::uint64_t id; WorldSession* session; Group* group; bool bg=false,queued=false; std::uint64_t GetPB59OwnerIncarnation(){return id;} WorldSession* GetSession(){return session;} Group* GetGroup(){return group;} bool InBattleground(){return bg;} bool InBattlegroundQueue(){return queued;} };
struct PlayerbotAI { Player* master; Player* GetMaster(){return master;} };
struct ObjectAccessor { static inline Player* live=nullptr; static Player* FindPlayer(ObjectGuid g){return g==1?live:nullptr;} };
struct PlayerbotsMgr { PlayerbotAI* ai=nullptr; static PlayerbotsMgr& instance(){static PlayerbotsMgr m;return m;} PlayerbotAI* GetPlayerbotAI(Player*){return ai;} };
bool IsRealPlayer(Player* p){return p!=nullptr;}
struct PlayerbotRepository { int saves=0; static PlayerbotRepository& instance(){static PlayerbotRepository r;return r;} void Save(PlayerbotAI*){++saves;} };
struct PlayerbotOperation { virtual ~PlayerbotOperation()=default; virtual bool Execute()=0; virtual ObjectGuid GetBotGuid() const=0; virtual uint32 GetPriority() const=0; virtual std::string GetName() const=0; virtual bool IsValid() const=0; };
"""
test = r"""
int main(){
 Group group; Player p{2,nullptr,&group}, other{4,nullptr,&group}; WorldSession s{&p,3}; p.session=&s; PlayerbotAI ai{&other}; PlayerbotsMgr::instance().ai=&ai;
 auto& repo=PlayerbotRepository::instance(); BotLogoutGroupCleanupOperation op(1,2,3);
 ObjectAccessor::live=&p; assert(op.IsValid()&&op.Execute()&&repo.saves==1);
 ObjectAccessor::live=&other; assert(!op.IsValid()&&!op.Execute());
 ObjectAccessor::live=&p; s.id=4; assert(!op.IsValid()&&!op.Execute()); s.id=3;
 s.player=&other; assert(!op.Execute()); s.player=&p;
 p.session=nullptr; assert(!op.Execute()); p.session=&s;
 ObjectAccessor::live=nullptr; assert(!op.Execute()); ObjectAccessor::live=&p;
 assert(!BotLogoutGroupCleanupOperation(1,0,3).Execute()); assert(!BotLogoutGroupCleanupOperation(1,2,0).Execute());
 PlayerbotsMgr::instance().ai=nullptr; assert(!op.Execute()); PlayerbotsMgr::instance().ai=&ai;
 p.bg=true; assert(op.Execute()&&repo.saves==1); p.bg=false; p.queued=true; assert(op.Execute()&&repo.saves==1);
}
"""
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "cleanup.cpp"
    exe = Path(tmp) / "cleanup"
    cpp.write_text(stub + source[start:end] + test)
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror"]
    if args.sanitizers:
        flags += ["-fsanitize=address,undefined"]
    subprocess.run([args.cxx, *flags, str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PB59 queued cleanup identity: PASS")
