// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestYaksue.h"
#include "TestYaksue/Private/TestYaksueCommands.h"
#include "D:\Work\UE426\Engine\Source\Runtime\Core\Public\Misc\MessageDialog.h"
#include "Framework\Commands\UICommandList.h"
#include "Framework\Commands\UIAction.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ContentBrowserModule.h"
#include "TestYaksueStyle.h"

#define LOCTEXT_NAMESPACE "FTestYaksueModule"

void FTestYaksueModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	//初始化Style
	FTestYaksueStyle::Initialize();
	FTestYaksueStyle::ReloadTextures();


	/**
	 * Register是TCommands的接口。用来注册命令，通常在模块的启动函数中调用。
	 * 观察它的实现可看到内部调用了RegisterCommands函数
	 */
	FTestYaksueCommands::Register();

	/**
	 * 借助UICommandList让 TCommands和操作action 执行关联绑定
	 */
	 //创建UICommandList,借助这个UICommandList管理 '某命令的实现效果'
	PluginCommandList = MakeShareable(new FUICommandList);
	//为命令映射操作; 把 命令'CommandA'与操作'CommandAAction'相关联
	PluginCommandList->MapAction(
		FTestYaksueCommands::Get().CommandA,
		FExecuteAction::CreateRaw(this, &FTestYaksueModule::CommandAAction),
		FCanExecuteAction()
	);

	/**
	 * 扩展关卡编辑器的菜单与工具栏
	 */
	 //获得关卡编辑器模块
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	//扩展关卡编辑器的菜单
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());// Extender:打算用作关卡编辑器的菜单
		MenuExtender->AddMenuExtension("WindowLayout"
			, EExtensionHook::After
			, PluginCommandList
			, FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& Builder) {
				Builder.AddMenuEntry(FTestYaksueCommands::Get().CommandA);
				}));
		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}
	//扩展关卡编辑器的工具栏
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender());// Extender:打算用作关卡编辑器的工具栏
		ToolbarExtender->AddToolBarExtension("Settings"
			, EExtensionHook::After
			, PluginCommandList
			, FToolBarExtensionDelegate::CreateLambda([](FToolBarBuilder& Builder) {
				Builder.AddToolBarButton(FTestYaksueCommands::Get().CommandA);
				}));
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}

	//将CommandA添加入关卡编辑器的GlobalLevelEditorActions中，这样可以触发快捷键
	/**
	 * 也就是说，关卡编辑器的FUICommandList将会在关卡编辑器内检测是否有快捷键。而之前在【实践：为命令映射具体的操作】中自己创建的FUICommandList并没有额外的检测快捷键是否触发的机制，所以没有任何快捷键效果
	 */
	{
		TSharedRef<FUICommandList> LevelEditorCommandList = LevelEditorModule.GetGlobalLevelEditorActions();// 拿取关卡编辑器的全局UICommandList
		LevelEditorCommandList->MapAction(
			FTestYaksueCommands::Get().CommandA,//哪条命令实例
			FExecuteAction::CreateRaw(this, &FTestYaksueModule::CommandAAction),//哪条操作效果
			FCanExecuteAction());
	}

	/// 有关内容浏览器模块
	{
		//获得内容浏览器模块
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));// 内容浏览器模块
		//加入内容浏览器的命令
		TArray<FContentBrowserCommandExtender>& CBCommandExtenderDelegates = ContentBrowserModule.GetAllContentBrowserCommandExtenders();// 取出所有Extender的委托,即一组委托
		CBCommandExtenderDelegates.Add(FContentBrowserCommandExtender::CreateLambda([this](TSharedRef<FUICommandList> CommandList, FOnContentBrowserGetSelection GetSelectionDelegate) {
				CommandList->MapAction(FTestYaksueCommands::Get().CommandB,
					FExecuteAction::CreateLambda([this, GetSelectionDelegate] {
						CommandBAction(GetSelectionDelegate);
						})
				);
			})
		);

		/**
		* 其他菜单拓展
		* UE4有多处菜单都可以拓展，例如
		*/
		// 增加内容浏览器中Asset右键菜单
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();// 从内容浏览器模块取出一堆有关asset的右键菜单委托
		// 注意此类型委托是 TSharedRef<FExtender> (const TArray<FAssetData>&)
		CBAssetMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateLambda([](const TArray<FAssetData>& SelectedAssets) {
			//添加菜单扩展
			TSharedRef<FExtender> Extender(new FExtender());
			Extender->AddMenuExtension(// 为某个Extender关联一套弹出的菜单
				"AssetContextReferences",// "AssetContextReferences"指定了命令将要添加到的分栏位置
				EExtensionHook::First,
				nullptr,// 而nullptr参数位置本来应该是一个FUICommandList，现在设置为空。是因为ContentBrowserModule已经拥有一个FUICommandList了，它在上一步中为CommandB映射过操作了
				FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& MenuBuilder) {
					MenuBuilder.AddMenuEntry(FTestYaksueCommands::Get().CommandB);
					})
			);

			return Extender;
			})
		);
	}


	///增加Asset编辑器中的菜单
	TArray<FAssetEditorExtender>& AssetEditorMenuExtenderDelegates = FAssetEditorToolkit::GetSharedMenuExtensibilityManager()->GetExtenderDelegates();
	AssetEditorMenuExtenderDelegates.Add(FAssetEditorExtender::CreateLambda([this](const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects) {
		//映射操作
		CommandList->MapAction(
			FTestYaksueCommands::Get().CommandA,
			FExecuteAction::CreateRaw(this, &FTestYaksueModule::CommandAAction),
			FCanExecuteAction());
		//添加菜单扩展
		TSharedRef<FExtender> Extender(new FExtender());
		Extender->AddMenuExtension(
			"FindInContentBrowser",
			EExtensionHook::After,
			CommandList,
			FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& MenuBuilder) {
				MenuBuilder.AddMenuEntry(FTestYaksueCommands::Get().CommandA);
				})
		);
		return Extender;
		})
	);



}

void FTestYaksueModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	/**
	 * Unregister是TCommands的接口。负责清理所有和这组命令相关的资源，通常在模块的关闭函数中被调用。
	 */
	FTestYaksueCommands::Unregister();
}


void FTestYaksueModule::CommandAAction()
{
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Execute CommandA"));
}

// 除了关卡编辑器，“内容浏览器”也有类似的FUICommandList。它在触发命令的时候还可以知道所选择的资源是哪些
void FTestYaksueModule::CommandBAction(FOnContentBrowserGetSelection GetSelectionDelegate)
{
	//获得当前选择的资源
	TArray<FAssetData> SelectedAssets;
	TArray<FString> SelectedPaths;
	if (GetSelectionDelegate.IsBound())
		GetSelectionDelegate.Execute(SelectedAssets, SelectedPaths);

	//要显示的信息：
	FString Message = "Execute CommandB:";
	for (auto ad : SelectedAssets)
		Message += ad.GetAsset()->GetName() + " ";
	//显示对话框
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTestYaksueModule, TestYaksue)