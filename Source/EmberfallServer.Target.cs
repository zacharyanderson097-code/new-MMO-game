using UnrealBuildTool;

public class EmberfallServerTarget : TargetRules
{
    public EmberfallServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
        ExtraModuleNames.Add("Emberfall");
    }
}
