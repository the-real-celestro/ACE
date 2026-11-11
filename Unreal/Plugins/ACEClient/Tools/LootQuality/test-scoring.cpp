#include "../../Source/ACEClient/Public/ACELootScoring.h"
#include <cassert>
#include <iostream>
#include <limits>

int main()
{
    using namespace ACELootScoring;
    const Range Mean{39.33,49.725},Mod{1.08,1.20};
    auto Core=[&](double Damage,double Variance,double Attack,double Defense)
    {return Melee(MeanHit(Damage,Variance),Attack,Defense,Mean,Mod,Mod).Total();};
    assert(std::abs(Core(57,.62,1.08,1.08))<1.e-8);
    assert(std::abs(Core(65,.47,1.08,1.08)-85)<1.e-8);
    assert(std::abs(Core(65,.47,1.20,1.20)-100)<1.e-8);
    assert(Band(85)==Grade::Rare&&Band(93)==Grade::Epic&&Band(98)==Grade::Legendary);
    assert(Core(65,.62,1.20,1.20)<Core(65,.47,1.08,1.08));
    assert(Core(63,.50,1.16,1.20)+6+5+5>90);
    assert(Overall(100,30)==130);
    assert(CantripPoints(Role::Irrelevant,35,35)==0);
    assert(std::abs(CantripPoints(Role::Damage,7,10)-5.6)<1.e-8);
    assert(std::abs(CantripPoints(Role::MagicDefense,25,35)-25./7)<1.e-8);
    assert(CantripPoints(Role::Defender,.09,.09,true)==12);
    assert(Position(std::numeric_limits<double>::quiet_NaN(),Mod)==0);
    assert(!Mod.Contains(std::numeric_limits<double>::infinity()));
    int Checks=0;
    for(int Damage=57;Damage<=65;++Damage)for(double Variance:{.47,.50,.53,.57,.62})
        for(int Attack=108;Attack<=120;++Attack)for(int Defense=108;Defense<=120;++Defense)
    {
        double Score=Core(Damage,Variance,Attack/100.,Defense/100.);
        assert(Score>=-1.e-7&&Score<=100+1.e-7);
        if(Damage<65)assert(Core(Damage+1,Variance,Attack/100.,Defense/100.)>=Score);
        if(Attack<120)assert(Core(Damage,Variance,(Attack+1)/100.,Defense/100.)>=Score);
        if(Defense<120)assert(Core(Damage,Variance,Attack/100.,(Defense+1)/100.)>=Score);
        assert(Overall(Score,5)>=Score);++Checks;
    }
    std::cout<<"Passed endpoints, grade boundaries, relevance, uncapped ranking and "<<Checks<<" weapon combinations.\n";
}
