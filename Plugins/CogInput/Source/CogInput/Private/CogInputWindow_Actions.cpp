#include "CogInputWindow_Actions.h"

#include "CogInputDataAsset.h"
#include "CogWindowHelper.h"
#include "CogWindowWidgets.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"

//--------------------------------------------------------------------------------------------------------------------------
void FCogInputWindow_Actions::Initialize()
{
    Super::Initialize();

    bHasMenu = true;

    Asset = GetAsset<UCogInputDataAsset>();
    Config = GetConfig<UCogInputConfig_Actions>();
}

//--------------------------------------------------------------------------------------------------------------------------
void FCogInputWindow_Actions::RenderHelp()
{
    ImGui::Text(
        "This window displays the current state of each Input Action. "
        "It can also be used to inject inputs to help debugging. "
        "The input action are read from a Input Mapping Context defined in '%s' data asset. "
        , TCHAR_TO_ANSI(*GetNameSafe(Asset.Get()))
    );
}

//--------------------------------------------------------------------------------------------------------------------------
void FCogInputWindow_Actions::ResetConfig()
{
    Super::ResetConfig();

    Config->Reset();
}

//--------------------------------------------------------------------------------------------------------------------------
void FCogInputWindow_Actions::RenderContent()
{
    Super::RenderContent();

    if (Asset == nullptr)
    {
        ImGui::Text("No Actions Asset");
        return;
    }

    ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
    if (LocalPlayer == nullptr)
    {
        ImGui::Text("No Local Player");
        return;
    }

    UEnhancedInputLocalPlayerSubsystem* EnhancedInputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
    if (EnhancedInputSubsystem == nullptr)
    {
        ImGui::Text("No Enhanced Input Subsystem");
        return;
    }
    
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Options"))
        {
            ImGui::SliderFloat("##Repeat", &Config->RepeatPeriod, 0.1f, 10.0f, "Repeat: %0.1fs");
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Reset"))
        {
            for (FCogInjectActionInfo& ActionInfo : Actions)
            {
                ActionInfo.Reset();
            }
        }

        ImGui::EndMenuBar();
    }

    if (Actions.Num() == 0)
    {
        for (TObjectPtr<const UInputMappingContext> MappingContext : Asset->MappingContexts)
        {
            for (const FEnhancedActionKeyMapping& Mapping : MappingContext->GetMappings())
            {
                if (Mapping.Action != nullptr && Actions.ContainsByPredicate([&Mapping](const FCogInjectActionInfo& ActionInfo) { return Mapping.Action == ActionInfo.Action; }) == false)
                {
                    FCogInjectActionInfo& ActionInfo = Actions.AddDefaulted_GetRef();
                    ActionInfo.Action = Mapping.Action;
                }
            }
        }

        Actions.Sort([](const FCogInjectActionInfo& Lhs, const FCogInjectActionInfo& Rhs)
        {
            return GetNameSafe(Lhs.Action).Compare(GetNameSafe(Rhs.Action)) < 0;
        });
    }

    if (ImGui::BeginTable("Actions", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBodyUntilResize))
    {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Inject", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        int Index = 0;

        for (FCogInjectActionInfo& ActionInfo : Actions)
        {
            ImGui::PushID(Index);

            const auto ActionName = StringCast<ANSICHAR>(*ActionInfo.Action->GetName());

            FInputActionValue ActionValue = EnhancedInputSubsystem->GetPlayerInput()->GetActionValue(ActionInfo.Action);

            switch (ActionInfo.Action->ValueType)
            {
                case EInputActionValueType::Boolean: 
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", ActionName.Get());

                    const ImVec4 ActiveColor(1, 0.8f, 0, 1);
                    const ImVec4 InnactiveColor(0.3f, 0.3f, 0.3f, 1);
                    const ImVec2 ButtonSize(FCogWindowWidgets::GetFontWidth() * 10, 0);

                    ImGui::TableNextColumn();
                    ImGui::BeginDisabled();
                    bool Value = ActionValue.Get<bool>();
                    FCogWindowWidgets::ToggleButton(&Value, "Pressed##Value", "Released##Value", ActiveColor, InnactiveColor, ButtonSize);
                    ImGui::EndDisabled();

                    ImGui::TableNextColumn();
                    FCogWindowWidgets::ToggleButton(&ActionInfo.bPressed, "Pressed##Inject", "Released##Inject", ActiveColor, InnactiveColor, ButtonSize);
                    ImGui::SameLine();
                    FCogWindowWidgets::ToggleButton(&ActionInfo.bRepeat, "Repeat", "Repeat", ActiveColor, InnactiveColor, ButtonSize);
                    break;
                }

                case EInputActionValueType::Axis1D: 
                {
                    const float Value = ActionValue.Get<float>();
                    DrawAxis("%s", ActionName.Get(), Value, &ActionInfo.X);
                    break;
                }

                case EInputActionValueType::Axis2D:
                {
                    const FVector2D Value = ActionValue.Get<FVector2D>();
                    DrawAxis("%s X", ActionName.Get(), Value.X, &ActionInfo.X);
                    DrawAxis("%s Y", ActionName.Get(), Value.Y, &ActionInfo.Y);
                    break;
                }

                case EInputActionValueType::Axis3D:
                {
                    const FVector Value = ActionValue.Get<FVector>();
                    DrawAxis("%s X", ActionName.Get(), Value.X, &ActionInfo.X);
                    DrawAxis("%s Y", ActionName.Get(), Value.Y, &ActionInfo.Y);
                    DrawAxis("%s Z", ActionName.Get(), Value.Z, &ActionInfo.Z);
                    break;
                }
            }

            ImGui::PopID();
            Index++;
        }
        ImGui::EndTable();
    }
}

//--------------------------------------------------------------------------------------------------------------------------
void FCogInputWindow_Actions::RenderTick(float DeltaSeconds)
{
    Super::RenderTick(DeltaSeconds);

    if (GetWorld() == nullptr)
    {
        return;
    }

    ULocalPlayer* LocalPlayer = GetWorld()->GetFirstLocalPlayerFromController();
    if (LocalPlayer == nullptr)
    {
        return;
    }

    UEnhancedInputLocalPlayerSubsystem* EnhancedInput = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer);
    if (EnhancedInput == nullptr)
    {
        return;
    }

    bool IsTimeToRepeat = false;
    float WorldTime = GetWorld()->GetTimeSeconds();
    if (RepeatTime < WorldTime)
    {
        RepeatTime = WorldTime + Config->RepeatPeriod;
        IsTimeToRepeat = true;
    }

    for (FCogInjectActionInfo& ActionInfo : Actions)
    {
        ActionInfo.Inject(*EnhancedInput, IsTimeToRepeat);
    }
}

//--------------------------------------------------------------------------------------------------------------------------
void FCogInputWindow_Actions::DrawAxis(const char* Format, const char* ActionName, float CurrentValue, float* InjectValue)
{
    ImGui::PushID(Format);
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::Text(Format, ActionName);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    ImGui::BeginDisabled();
    ImGui::SliderFloat("##Value", &CurrentValue, -1.0f, 1.0f, "%0.2f");
    ImGui::EndDisabled();

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1);
    FCogWindowWidgets::SliderWithReset("##Inject", InjectValue, -1.0f, 1.0f, 0.0f, "%0.2f");
    ImGui::PopID();
}