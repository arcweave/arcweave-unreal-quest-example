using UnrealBuildTool;

public class ArcweaveQuest : ModuleRules
{
    public ArcweaveQuest(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "arcweave" });
        RuntimeDependencies.Add(System.IO.Path.Combine(ModuleDirectory, "../../LICENSE"), StagedFileType.NonUFS);
        RuntimeDependencies.Add(System.IO.Path.Combine(ModuleDirectory, "../../THIRD_PARTY_NOTICES.md"), StagedFileType.NonUFS);
    }
}
