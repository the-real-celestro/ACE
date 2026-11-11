#include "../../Source/ACEClient/Public/ACELootScoring.h"
#include <iomanip>
#include <iostream>
#include <vector>
using namespace ACELootScoring;
struct Magic { const char* Name;Role Effect;double Amount,Legendary; };
struct Example {const char* Id;const char* Name;double Damage,Variance,Attack,Defense;std::vector<Magic> Spells;const char* Finding;};
int main()
{
    const Magic Blood{"Legendary Blood Thirst",Role::Damage,10,10},Heart{"Legendary Heart Thirst",Role::Attack,.09,.09},
        Defender{"Legendary Defender",Role::Defender,.09,.09},Melee{"Legendary Invulnerability",Role::MeleeDefense,35,35},
        MagicDefense{"Legendary Magic Resistance",Role::MagicDefense,35,35},Skill{"Legendary Heavy Weapons Aptitude",Role::WeaponSkill,35,35},
        Cooking{"Legendary Cooking",Role::Irrelevant,35,35};
    const std::vector<Example> Examples={
        {"poor","Weak all around",57,.62,1.08,1.08,{},"Worst original rolls define the zero endpoint."},
        {"common","Low damage, some accuracy",59,.57,1.12,1.12,{},"Better accuracy cannot replace a strong damage roll."},
        {"ordinary","Middle of the bracket",61,.53,1.16,1.16,{},"Useful, but far from the best damage interval."},
        {"wide","Maximum number, wide variance",65,.62,1.20,1.20,{},"65 maximum is not perfect damage: its low end is only 24.7."},
        {"balanced","Good damage and defense",63,.50,1.16,1.20,{},"Strong damage and natural defense make this a Rare before cantrips."},
        {"defensive","Good damage, defensive package",63,.50,1.16,1.20,{Defender,Melee,MagicDefense},"Three distinct defensive effects lift this weapon into Epic."},
        {"damage","Perfect damage, weakest bonuses",65,.47,1.08,1.08,{},"Perfect damage retains Rare even with the lowest attack and defense."},
        {"cooking","Perfect damage, unrelated magic",65,.47,1.08,1.08,{Cooking},"Legendary Cooking adds utility, but zero combat-quality points."},
        {"modifiers","Perfect damage, attack and defense",65,.47,1.08,1.08,{Heart,Defender},"Attack and weapon defense substantially improve the same original roll."},
        {"blood","Perfect damage, Blood Thirst",65,.47,1.08,1.08,{Blood},"Legendary Blood Thirst pushes the damage specialist into Epic."},
        {"legendary","Perfect damage, matching offense",65,.47,1.08,1.08,{Blood,Skill},"Damage plus the correct weapon skill crosses the Legendary threshold."},
        {"elite","Perfect rolls, elite spell package",65,.47,1.20,1.20,{Blood,Defender,MagicDefense},"An exceptional Legendary stays measurably above ordinary Legendary items."}
    };
    std::cout<<std::fixed<<std::setprecision(6)<<"{\"scope\":\"Hypothetical untinkered Heavy Swords, 400 base wield, public ACE baseline. Combat-relevant potential; no duplicate suppression.\",\"weights\":{\"damage\":85,\"attack\":10,\"defense\":5},\"examples\":[";
    bool First=true;
    for(const auto& E:Examples)
    {
        const auto P=ACELootScoring::Melee(MeanHit(E.Damage,E.Variance),E.Attack,E.Defense,{39.33,49.725},{1.08,1.20},{1.08,1.20});
        double Bonus=0;for(const auto& S:E.Spells)Bonus+=CantripPoints(S.Effect,S.Amount,S.Legendary);
        if(!First)std::cout<<',';First=false;
        std::cout<<"{\"id\":\""<<E.Id<<"\",\"name\":\""<<E.Name<<"\",\"maximum\":"<<E.Damage<<",\"minimum\":"<<E.Damage*(1-E.Variance)
            <<",\"variance\":"<<E.Variance<<",\"mean\":"<<MeanHit(E.Damage,E.Variance)<<",\"attack\":"<<E.Attack-1<<",\"defense\":"<<E.Defense-1
            <<",\"damagePoints\":"<<P.Damage<<",\"attackPoints\":"<<P.Attack<<",\"defensePoints\":"<<P.Defense<<",\"original\":"<<P.Total()
            <<",\"cantripPoints\":"<<Bonus<<",\"total\":"<<Overall(P.Total(),Bonus)<<",\"grade\":\""<<Name(Band(Overall(P.Total(),Bonus)))<<"\",\"finding\":\""<<E.Finding<<"\",\"spells\":[";
        bool F=true;for(const auto& S:E.Spells){if(!F)std::cout<<',';F=false;std::cout<<"{\"name\":\""<<S.Name<<"\",\"points\":"<<CantripPoints(S.Effect,S.Amount,S.Legendary)<<'}';}
        std::cout<<"]}";
    }
    std::cout<<"]}\n";
}
