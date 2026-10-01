using UnrealBuildTool;

public class ArcweaveQuestTarget : TargetRules
{
    public ArcweaveQuestTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        // The released plugin assumes unity include order; keep it consistent across Git working sets.
        bUseAdaptiveUnityBuild = false;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
        ExtraModuleNames.Add("ArcweaveQuest");
    }
}
