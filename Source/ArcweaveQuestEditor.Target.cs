using UnrealBuildTool;

public class ArcweaveQuestEditorTarget : TargetRules
{
    public ArcweaveQuestEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        // The released plugin assumes unity include order; keep it consistent across Git working sets.
        bUseAdaptiveUnityBuild = false;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;
        ExtraModuleNames.Add("ArcweaveQuest");
    }
}
