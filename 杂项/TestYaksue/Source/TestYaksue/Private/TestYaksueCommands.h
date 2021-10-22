#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"

class FTestYaksueCommands :public TCommands<FTestYaksueCommands>
{
public:
	FTestYaksueCommands();

	// 重写TCommands<>的接口：注册命令
	virtual void RegisterCommands() override;

public:
	// 命令A
	TSharedPtr<FUICommandInfo> CommandA;// FTestYaksueCommands将容纳这个插件所有的命令，当前只有一个命令CommandA

	//命令B
	TSharedPtr< FUICommandInfo > CommandB;
protected:
private:
};