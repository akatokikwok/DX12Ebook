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

	/** Ϊ����ӳ�����Ĳ���
	 * ��FTestYaksueModule������һ����������ָ��CommandA����Ӧ�Ĳ���; �������ľ��ǵ���һ���Ի���
	 */
	void CommandAAction();//CommandA����Ӧ�Ĳ���

	void CommandBAction(FOnContentBrowserGetSelection GetSelectionDelegate);//CommandB����Ӧ�Ĳ���
public:
	// ӳ�䡱������Ҫ��FUICommandList���
	TSharedPtr<class FUICommandList> PluginCommandList;
};
