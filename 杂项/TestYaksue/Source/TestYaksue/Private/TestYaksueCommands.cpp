#include "TestYaksueCommands.h"

#define LOCTEXT_NAMESPACE "FTestYaksueModule"

/**
 * 参数1：const FName InContextName。现在是"TestYaksueCommands"
 * 参数2：const FText& InContextDesc。现在是NSLOCTEXT("Contexts", "TestYaksueCommands", "TestYaksue Plugin")
 * 参数3：const FName InContextParent。现在是NAME_None
 * 参数4：const FName InStyleSetName。这里留意，他和图标有关，现在暂时是FName(*FString("todo"))，如果需要图标，则这里需要改动。
 */
FTestYaksueCommands::FTestYaksueCommands()
	: TCommands<FTestYaksueCommands>(
		"TestYaksueCommands",
		NSLOCTEXT("Contexts", "TestYaksueCommands", "TestYaksue Plugin"),
		NAME_None,
		FName(*FString("todo"))
		)
{
	//UI_COMMAND(CommandA, "TestYaksueCommandA", "Execute TestYaksue CommandA", EUserInterfaceActionType::Button, FInputChord());
}


/************************************************************************/
/* RegisterCommands中将使用UI_COMMAND宏注册自己所有命令。留意最后一个参数，现在是FInputGesture()，它和快捷键有关;
* Shift + Alt+Z是这个命令的快捷键
* 为了使这个快捷键有效，需要将这个命令使用某个“特殊”的FUICommandList中映射
/************************************************************************/
void FTestYaksueCommands::RegisterCommands()
{
	UI_COMMAND(CommandA, "TestYaksueCommandA+++ 名字", "TestYaksueCommandA+++的动作注释", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift | EModifierKey::Alt, EKeys::Z)/*最后一个参数是快捷键*/);

	// 除了关卡编辑器，“内容浏览器”也有类似的FUICommandList。它在触发命令的时候还可以知道所选择的资源是哪些
	UI_COMMAND(CommandB, "TestYaksueCommandB", "Execute TestYaksue CommandB", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Shift | EModifierKey::Alt, EKeys::X));
}

#undef LOCTEXT_NAMESPACE



