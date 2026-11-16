#include <cassert>
#include <functional>
#include <stdexcept>
struct Group {};
struct Player { Group* group=nullptr; bool bg=false, queued=false; Group* GetGroup(){return group;} bool InBattleground(){return bg;} bool InBattlegroundQueue(){return queued;} };
struct AI { Player* master=nullptr; Player* GetMaster(){return master;} };
bool IsRealPlayer(Player* p){return p!=nullptr;}
struct Journal { enum class Step { BeforeRepositorySave }; };
struct Record { int calls=0; void MarkStep(Journal::Step s){assert(s==Journal::Step::BeforeRepositorySave);++calls;} };
struct PlayerbotRepository { int saves=0; std::function<void()> hook; static PlayerbotRepository& instance(){static PlayerbotRepository repo;return repo;} void Save(AI*){++saves;if(hook)hook();} };
enum class Result { Complete, Recovery };
Result Retire(Player* bot, AI* botAI, Record& record, bool& exact, bool& detached)
{
    auto current=[&]{return exact;};
    auto partial=[] {return Result::Recovery;};
    try {
#include "retirement_save.inc"
        detached=true;
        return Result::Complete;
    } catch(...) { return partial(); }
}
int main(){
    Group group; Player master, bot{&group}; AI ai{&master}; auto& repo=PlayerbotRepository::instance();
    for(int n=0;n<7;++n){
        Record record; bool exact=true,detached=false; bot={&group,false,false};ai.master=&master;repo.saves=0;repo.hook={};
        if(n==1)bot.group=nullptr;
        if(n==2)bot.bg=true;
        if(n==3)bot.queued=true;
        if(n==4)ai.master=nullptr;
        if(n==5)repo.hook=[&]{exact=false;};
        if(n==6)repo.hook=[] {throw std::runtime_error("save");};
        auto result=Retire(&bot,&ai,record,exact,detached);
        assert(record.calls==1);assert(repo.saves==((n==0||n>=5)?1:0));
        assert(detached==(n<5));assert(result==(n<5?Result::Complete:Result::Recovery));
    }
    repo.hook={};
}
