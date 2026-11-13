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
    assert(CantripPoints(Role::Defender,.09,.09,true)==14);
    assert(Position(std::numeric_limits<double>::quiet_NaN(),Mod)==0);
    assert(std::abs(ArmorBanePoints(.1,.2)-.2)<1.e-8);
    assert(std::abs(ArmorBanePoints(1.9,.2)-.1)<1.e-8);
    assert(ArmorBanePoints(2.,.25)==0);
    assert(ArmorBanePoints(.1,-.2)==0);
    assert(ArmorBanePoints(std::numeric_limits<double>::quiet_NaN(),.2)==0);
    const double Complete[7]={2,2,2,2,2,2,2},OverCap[7]={3,3,3,3,3,3,3},Weak[7]={1.3,1.3,1.3,.6,.6,.6,.6};
    assert(std::abs(Covenant(529,{500,529},240,Complete).Total()-80)<1.e-8);
    assert(Covenant(529,{500,529},320,Complete).Total()>80);
    assert(Covenant(529,{500,529},240,Complete).Total()==Covenant(529,{500,529},240,OverCap).Total());
    assert(Covenant(529,{500,529},0,Weak).Total()<Covenant(529,{500,529},240,Complete).Total());
    assert(WearableRelevanceFactor(Role::Armor,false,true)==.90);
    assert(WearableRelevanceFactor(Role::Armor,true)==1.05);
    assert(EquipmentSetPoints(Role::WeaponSkill,20)==24);
    assert(EquipmentSetPoints(Role::MagicDefense,5)==6);
    assert(EquipmentSetPoints(Role::Irrelevant,20)==0);
    assert(EquipmentSetPoints(Role::WeaponSkill,100)==24);
    assert(std::abs(68*.94*WearableRelevanceFactor(Role::Armor,false,true)+EquipmentSetPoints(Role::WeaponSkill,20)-81.528)<1.e-8);
    assert(!Mod.Contains(std::numeric_limits<double>::infinity()));
    assert(SpellUsefulness(4,1)==Grade::Legendary);
    assert(SpellUsefulness(3,1)==Grade::Epic);
    assert(SpellUsefulness(4,.8)==Grade::Rare);
    assert(SpellUsefulness(4,0)==Grade::Poor);
    assert(SpellUsefulness(1,0)==Grade::Poor);
    for(int Property=370;Property<=379;++Property)
    {
        assert(NativeGearPoints(Property,0)==0);
        assert(NativeGearPoints(Property,1)>0&&NativeGearPoints(Property,2)>NativeGearPoints(Property,1));
        assert(NativeGearPoints(Property,3)>NativeGearPoints(Property,2));
        assert(NativeGearPoints(Property,2,2)==NativeGearPoints(Property,3,3));
        assert(NativeGearPoints(Property,3)>=25);
    }
    assert(NativeGearPoints(379,3)==50&&NativeGearPoints(376,3)==45);
    assert(NativeGearPoints(370,3)==45&&NativeGearPoints(375,2)==21);
    assert(WearableRelevanceFactor(Role::SurvivalAttribute)>WearableRelevanceFactor(Role::Shield));
    assert(WearableRelevanceFactor(Role::BaselineAttribute)>0);
    int Pet[6]={9,9,0,5,8,0};
    assert(Essence(Pet).Valid&&Essence(Pet).Total==31);
    assert(std::abs(Essence(Pet).Points()-100.*31/120)<1.e-8);
    for(int Value=0;Value<=20;++Value)
    {
        for(int I=0;I<6;++I)Pet[I]=Value;
        assert(std::abs(Essence(Pet).Points()-5.*Value)<1.e-8);
        for(int I=0;I<6&&Value<20;++I)
        {++Pet[I];assert(Essence(Pet).Points()>5.*Value);--Pet[I];}
    }
    Pet[0]=21;assert(!Essence(Pet).Valid);
    Pet[0]=-1;assert(!Essence(Pet).Valid);
    TinkerAxis Axes[]={{67,{57,65},1,1,.55},{.376,{.47,.62},0,.8,.25},{1.08,{1.08,1.2},.01,1,.08},{1.08,{1.08,1.2},.01,1,.12}};
    auto Guess=InferTinkers(Axes,4,3,0,[&](const double* V){return Core(V[0],V[1],V[2],V[3]);});
    assert(Guess.Valid&&Guess.Operations[0]==2&&Guess.Operations[1]==1);
    assert(std::abs(Guess.Base[0]-65)<1.e-8&&std::abs(Guess.Base[1]-.47)<1.e-8);
    assert(Guess.Low<=85&&Guess.High>=85);
    assert(!InferTinkers(Axes,4,3,1,[](const double*){return 0.;}).Valid);
    Axes[0].Current=std::numeric_limits<double>::quiet_NaN();
    assert(!InferTinkers(Axes,4,3,0,[](const double*){return 0.;}).Valid);
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
