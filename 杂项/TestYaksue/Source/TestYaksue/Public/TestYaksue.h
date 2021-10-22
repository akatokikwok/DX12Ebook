// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "D:\Work\UE426\Engine\Source\Runtime\Slate\Public\Framework\Commands\UICommandList.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "D:\Work\UE426\Engine\Source\Editor\ContentBrowser\Public\ContentBrowserDelegates.h"

class FTestYaksueModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 为命令映射具体的操作
	 * 在FTestYaksueModule中声明一个函数用来指定CommandA所对应的操作; 具体做的就是弹出一个对话框
	 */
	void CommandAAction();//CommandA所对应的操作

	void CommandBAction(FOnContentBrowserGetSelection GetSelectionDelegate);//CommandB所对应的操作
public:
	// 映射”操作需要由FUICommandList完成
	TSharedPtr<class FUICommandList> PluginCommandList;
};
