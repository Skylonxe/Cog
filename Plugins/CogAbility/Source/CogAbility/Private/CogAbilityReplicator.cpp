#include "CogAbilityReplicator.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "CogAbilityDataAsset.h"
#include "CogWindowHelper.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"

//--------------------------------------------------------------------------------------------------------------------------
ACogAbilityReplicator* ACogAbilityReplicator::Spawn(APlayerController* Controller)
{
    if (Controller->GetWorld()->GetNetMode() == NM_Client)
    {
        return nullptr;
    }

    FActorSpawnParameters SpawnInfo;
    SpawnInfo.Owner = Controller;
    return Controller->GetWorld()->SpawnActor<ACogAbilityReplicator>(SpawnInfo);
}

//--------------------------------------------------------------------------------------------------------------------------
ACogAbilityReplicator* ACogAbilityReplicator::GetLocalReplicator(UWorld& World)
{
    for (TActorIterator<ACogAbilityReplicator> It(&World, ACogAbilityReplicator::StaticClass()); It; ++It)
    {
        ACogAbilityReplicator* Replicator = *It;
        return Replicator;
    }

    return nullptr;
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::GetRemoteReplicators(UWorld& World, TArray<ACogAbilityReplicator*>& Replicators)
{
    for (TActorIterator<ACogAbilityReplicator> It(&World, ACogAbilityReplicator::StaticClass()); It; ++It)
    {
        ACogAbilityReplicator* Replicator = Cast<ACogAbilityReplicator>(*It);
        Replicators.Add(Replicator);
    }
}

//--------------------------------------------------------------------------------------------------------------------------
ACogAbilityReplicator::ACogAbilityReplicator(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
#if !UE_BUILD_SHIPPING

    bReplicates = true;
    bOnlyRelevantToOwner = true;

    AbilityAsset = FCogWindowHelper::GetFirstAssetByClass<UCogAbilityDataAsset>();

#endif // !UE_BUILD_SHIPPING
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    FDoRepLifetimeParams Params;
    Params.bIsPushBased = true;
    Params.Condition = COND_OwnerOnly;

    DOREPLIFETIME_WITH_PARAMS_FAST(ACogAbilityReplicator, TweakCurrentValues, Params);
    DOREPLIFETIME_WITH_PARAMS_FAST(ACogAbilityReplicator, TweakProfileIndex, Params);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::BeginPlay()
{
    Super::BeginPlay();

    OwnerPlayerController = Cast<APlayerController>(GetOwner());
    
    OnAnyActorSpawnedHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ACogAbilityReplicator::OnAnyActorSpawned));

    ApplyAllTweaksOnAllActors();
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorld()->RemoveOnActorSpawnedHandler(OnAnyActorSpawnedHandle);

    Super::EndPlay(EndPlayReason);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::ApplyCheat(AActor* CheatInstigator, const TArray<AActor*>& Targets, const FCogAbilityCheat& Cheat)
{
    Server_ApplyCheat(CheatInstigator, Targets, Cheat);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::Server_ApplyCheat_Implementation(const AActor* CheatInstigator, const TArray<AActor*>& Targets, const FCogAbilityCheat& Cheat) const
{
    UAbilitySystemComponent* InstigatorAbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CheatInstigator, true);
    if (InstigatorAbilitySystem == nullptr)
    {
        return;
    }

    for (AActor* Target : Targets)
    {
        UAbilitySystemComponent* TargetAbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target, true);
        if (TargetAbilitySystem == nullptr)
        {
            continue;
        }

        if (TargetAbilitySystem->GetGameplayEffectCount(Cheat.Effect, nullptr) > 0)
        {
            TargetAbilitySystem->RemoveActiveGameplayEffectBySourceEffect(Cheat.Effect, nullptr);
        }
        else
        {
            FGameplayEffectContextHandle ContextHandle = InstigatorAbilitySystem->MakeEffectContext();
            ContextHandle.AddSourceObject(InstigatorAbilitySystem);
            FGameplayEffectSpecHandle SpecHandle = InstigatorAbilitySystem->MakeOutgoingSpec(Cheat.Effect, 1, ContextHandle);

            if (FGameplayEffectSpec* EffectSpec = SpecHandle.Data.Get())
            {
                FHitResult HitResult;
                HitResult.HitObjectHandle = FActorInstanceHandle(Target);
                HitResult.Normal = FVector::ForwardVector;
                HitResult.ImpactNormal = FVector::ForwardVector;
                HitResult.Location = Target->GetActorLocation();
                HitResult.ImpactPoint = Target->GetActorLocation();
                HitResult.PhysMaterial = nullptr;
                ContextHandle.AddHitResult(HitResult, true);

                InstigatorAbilitySystem->ApplyGameplayEffectSpecToTarget(*EffectSpec, TargetAbilitySystem);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------
bool ACogAbilityReplicator::IsCheatActive(const AActor* EffectTarget, const FCogAbilityCheat& Cheat)
{
    if (Cheat.Effect == nullptr)
    {
        return false;
    }

    UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(EffectTarget, true);
    if (AbilitySystem == nullptr)
    {
        return false;
    }

    const int32 Count = AbilitySystem->GetGameplayEffectCount(Cheat.Effect, nullptr);
    return Count > 0;
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::GiveAbility(AActor* Target, TSubclassOf<UGameplayAbility> AbilityClass) const
{
    Server_GiveAbility(Target, AbilityClass);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::Server_GiveAbility_Implementation(AActor* TargetActor, TSubclassOf<UGameplayAbility> AbilityClass) const
{
    UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor, true);

    if (AbilitySystem == nullptr)
    {
        return;
    }

    if (AbilitySystem->IsOwnerActorAuthoritative() == false)
    {
        return;
    }

    if (IsValid(AbilityClass) == false)
    {
        return;
    }

    const FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, TargetActor);
    AbilitySystem->GiveAbility(Spec);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::RemoveAbility(AActor* TargetActor, const FGameplayAbilitySpecHandle& Handle) const
{
    Server_RemoveAbility(TargetActor, Handle);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::Server_RemoveAbility_Implementation(AActor* TargetActor, const FGameplayAbilitySpecHandle& Handle) const
{
    UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor, true);

    if (AbilitySystem == nullptr)
    {
        return;
    }
    
    if (AbilitySystem->IsOwnerActorAuthoritative() == false)
    {
        return;
    }

    if (Handle.IsValid() == false)
    {
        return;
    }

    AbilitySystem->SetRemoveAbilityOnEnd(Handle);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::ResetAllTweaks()
{
    Server_ResetAllTweaks();
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::Server_ResetAllTweaks_Implementation()
{
    TweakCurrentValues.Empty();
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::OnAnyActorSpawned(AActor* Actor)
{
    if (AbilityAsset->ActorRootClass != nullptr && Actor->GetClass()->IsChildOf(AbilityAsset->ActorRootClass) == false)
    {
        return;
    }

    int32 TweakCategoryIndex = FindTweakCategoryFromActor(Actor);
    if (TweakCategoryIndex == INDEX_NONE)
    {
        return;
    }

    ApplyAllTweaksOnActor(TweakCategoryIndex, Actor);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::SetTweakValue(int32 TweakIndex, int32 TweakCategoryIndex, float Value)
{
    Server_SetTweakValue(TweakIndex, TweakCategoryIndex, Value);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::Server_SetTweakValue_Implementation(int32 TweakIndex, int32 TweakCategoryIndex, float Value)
{
    if (AbilityAsset == nullptr)
    {
        return;
    }

    if (AbilityAsset->Tweaks.IsValidIndex(TweakIndex) == false)
    {
        return;
    }

    if (AbilityAsset->TweaksCategories.IsValidIndex(TweakCategoryIndex) == false)
    {
        return;
    }

    SetTweakCurrentValue(TweakIndex, TweakCategoryIndex, Value);

    const FCogAbilityTweak& Tweak = AbilityAsset->Tweaks[TweakIndex];

    TArray<AActor*> Actors;
    FindActorsFromTweakCategory(TweakCategoryIndex, Actors);
    for (AActor* Actor : Actors)
    {
        ApplyTweakOnActor(Actor, Tweak, Value, AbilityAsset->SetByCallerMagnitudeTag);
    }
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::ApplyAllTweaksOnActor(int32 TweakCategoryIndex, AActor* Actor)
{
    if (AbilityAsset == nullptr)
    {
        return;
    }

    if (AbilityAsset->TweaksCategories.IsValidIndex(TweakCategoryIndex) == false)
    {
        return;
    }

    int32 TweakIndex = 0;
    for (const FCogAbilityTweak& Tweak : AbilityAsset->Tweaks)
    {
        const float Value = GetTweakCurrentValue(TweakIndex, TweakCategoryIndex);
        ApplyTweakOnActor(Actor, Tweak, Value, AbilityAsset->SetByCallerMagnitudeTag);
        TweakIndex++;
    }
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::ApplyTweakOnActor(AActor* Actor, const FCogAbilityTweak& Tweak, float Value, const FGameplayTag& SetByCallerMagnitudeTag)
{
    UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, true);
    if (AbilitySystem == nullptr)
    {
        return;
    }

    AbilitySystem->RemoveActiveGameplayEffectBySourceEffect(Tweak.Effect, nullptr);

    if (Value != 0.0f)
    {
        FGameplayEffectSpecHandle SpecHandle = AbilitySystem->MakeOutgoingSpec(Tweak.Effect, 1, AbilitySystem->MakeEffectContext());

        if (FGameplayEffectSpec* EffectSpec = SpecHandle.Data.Get())
        {
            EffectSpec->SetSetByCallerMagnitude(SetByCallerMagnitudeTag, (Value * Tweak.Multiplier) + Tweak.AddPostMultiplier);
            AbilitySystem->ApplyGameplayEffectSpecToSelf(*EffectSpec);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------
float ACogAbilityReplicator::GetTweakCurrentValue(int32 TweakIndex, int32 TweakCategoryIndex)
{
    float* Value = GetTweakCurrentValuePtr(TweakIndex, TweakCategoryIndex);
    if (Value == nullptr)
    {
        return 0.0f; 
    }
    
    return *Value;
}

//--------------------------------------------------------------------------------------------------------------------------
float* ACogAbilityReplicator::GetTweakCurrentValuePtr(int32 TweakIndex, int32 TweakCategoryIndex)
{
    if (AbilityAsset == nullptr)
    {
        return nullptr;
    }

    TweakCurrentValues.SetNum(AbilityAsset->Tweaks.Num() * AbilityAsset->TweaksCategories.Num());

    const int32 Index = TweakIndex + (TweakCategoryIndex * AbilityAsset->Tweaks.Num());

    if (TweakCurrentValues.IsValidIndex(Index) == false)
    {
        return nullptr;
    }

    return &TweakCurrentValues[Index];
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::SetTweakCurrentValue(int32 TweakIndex, int32 TweakCategoryIndex, float Value)
{
    if (AbilityAsset == nullptr)
    {
        return;
    }

    TweakCurrentValues.SetNum(AbilityAsset->Tweaks.Num() * AbilityAsset->TweaksCategories.Num());

    const int32 Index = TweakIndex + (TweakCategoryIndex * AbilityAsset->Tweaks.Num());
    if (TweakCurrentValues.IsValidIndex(TweakIndex) == false)
    {
        return;
    }

    TweakCurrentValues[Index] = Value;
    MARK_PROPERTY_DIRTY_FROM_NAME(ACogAbilityReplicator, TweakCurrentValues, this);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::SetTweakProfile(int32 ProfileIndex)
{
    Server_SetTweakProfile(ProfileIndex);
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::Server_SetTweakProfile_Implementation(int32 ProfileIndex)
{
    if (AbilityAsset == nullptr)
    {
        return;
    }

    if (AbilityAsset->TweakProfiles.IsValidIndex(ProfileIndex) == false)
    {
        ProfileIndex = INDEX_NONE;
    }

    TweakProfileIndex = ProfileIndex;
    MARK_PROPERTY_DIRTY_FROM_NAME(ACogAbilityReplicator, TweakProfileIndex, this);

    ResetAllTweaks();

    if (AbilityAsset->TweakProfiles.IsValidIndex(TweakProfileIndex))
    {
        const FCogAbilityTweakProfile& TweakProfile = AbilityAsset->TweakProfiles[TweakProfileIndex];

        for (const FCogAbilityTweakProfileValue& ProfileTweak : TweakProfile.Tweaks)
        {
            const int32 TweakIndex = AbilityAsset->Tweaks.IndexOfByPredicate([ProfileTweak](const FCogAbilityTweak& Tweak) { return ProfileTweak.Effect == Tweak.Effect; });
            const int32 TweakCategoryIndex = AbilityAsset->TweaksCategories.IndexOfByPredicate([ProfileTweak](const FCogAbilityTweakCategory& TweakCategory) { return ProfileTweak.CategoryId == TweakCategory.Id; });

            if (TweakIndex != INDEX_NONE && TweakCategoryIndex != INDEX_NONE)
            {
                SetTweakCurrentValue(TweakIndex, TweakCategoryIndex, ProfileTweak.Value);
            }
        }
    }

    ApplyAllTweaksOnAllActors();
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::ApplyAllTweaksOnAllActors()
{
    if (AbilityAsset == nullptr)
    {
        return;
    }

    for (int32 TweakCategoryIndex = 0; TweakCategoryIndex < AbilityAsset->TweaksCategories.Num(); ++TweakCategoryIndex)
    {
        TArray<AActor*> Actors;
        FindActorsFromTweakCategory(TweakCategoryIndex,  Actors);
        
        for (AActor* Actor : Actors)
        {
            ApplyAllTweaksOnActor(TweakCategoryIndex, Actor);
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------
void ACogAbilityReplicator::FindActorsFromTweakCategory(int32 TweakCategoryIndex, TArray<AActor*>& Actors)
{
    if (AbilityAsset == nullptr)
    {
        return;
    }
    
    if (AbilityAsset->TweaksCategories.IsValidIndex(TweakCategoryIndex) == false)
    {
        return;
    }
    
    const FCogAbilityTweakCategory& TweakCategory = AbilityAsset->TweaksCategories[TweakCategoryIndex];

    for (TActorIterator<AActor> It(GetWorld(), TweakCategory.ActorClass); It; ++It)
    {
        AActor* Actor = *It;
        if (UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, true))
        {
            const bool bHasRequiredTags = AbilitySystem->HasAllMatchingGameplayTags(TweakCategory.RequiredTags);
            const bool bHasIgnoredTags = AbilitySystem->HasAnyMatchingGameplayTags(TweakCategory.IgnoredTags);
            if (bHasRequiredTags && bHasIgnoredTags == false)
            {
                Actors.AddUnique(Actor);
            }
        }
    }
}

//--------------------------------------------------------------------------------------------------------------------------
int32 ACogAbilityReplicator::FindTweakCategoryFromActor(AActor* Actor)
{
    UAbilitySystemComponent* AbilitySystem = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, true);
    if (AbilitySystem == nullptr)
    {
        return INDEX_NONE;
    }

    for (int32 i = 0; i < AbilityAsset->TweaksCategories.Num(); ++i)
    {
        const FCogAbilityTweakCategory& TweakCategory = AbilityAsset->TweaksCategories[i];

        if (IsActorMatchingTweakCategory(Actor, AbilitySystem, TweakCategory))
        {
            return i;
        }
    }

    return INDEX_NONE;
}

//--------------------------------------------------------------------------------------------------------------------------
bool ACogAbilityReplicator::IsActorMatchingTweakCategory(const AActor* Actor, const UAbilitySystemComponent* ActorAbilitySystem, const FCogAbilityTweakCategory& TweakCategory)
{
    if (Actor->GetClass()->IsChildOf(TweakCategory.ActorClass) == false)
    {
        return false;
    }

    const bool bHasRequiredTags = ActorAbilitySystem->HasAllMatchingGameplayTags(TweakCategory.RequiredTags);
    if (bHasRequiredTags == false)
    {
        return false;
    }

    const bool bHasIgnoredTags = ActorAbilitySystem->HasAnyMatchingGameplayTags(TweakCategory.IgnoredTags);
    if (bHasIgnoredTags)
    {
        return false;
    }

    return true;
}