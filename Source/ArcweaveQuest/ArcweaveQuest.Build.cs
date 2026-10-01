using UnrealBuildTool;

public class ArcweaveQuest : ModuleRules
{
    public ArcweaveQuest(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "arcweave" });
    }
}
