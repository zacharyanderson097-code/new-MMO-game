using UnrealBuildTool;

public class EmberfallEditorTarget : TargetRules
{
    public EmberfallEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("Emberfall");
    }
}
