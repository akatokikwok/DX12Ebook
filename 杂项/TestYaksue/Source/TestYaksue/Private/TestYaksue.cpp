// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestYaksue.h"
#include "TestYaksue/Private/TestYaksueCommands.h"
#include "D:\Work\UE426\Engine\Source\Runtime\Core\Public\Misc\MessageDialog.h"
#include "Framework\Commands\UICommandList.h"
#include "Framework\Commands\UIAction.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ContentBrowserModule.h"

#define LOCTEXT_NAMESPACE "FTestYaksueModule"

void FTestYaksueModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	/**
	 * Register��TCommands�Ľӿڡ�����ע�����ͨ����ģ������������е��á�
	 * �۲�����ʵ�ֿɿ����ڲ�������RegisterCommands����
	 */
	FTestYaksueCommands::Register();

	/**
	 * ����UICommandList�� TCommands�Ͳ���action ִ�й�����
	 */
	 //����UICommandList,�������UICommandList���� 'ĳ�����ʵ��Ч��'
	PluginCommandList = MakeShareable(new FUICommandList);
	//Ϊ����ӳ�����; �� ����'CommandA'�����'CommandAAction'�����
	PluginCommandList->MapAction(
		FTestYaksueCommands::Get().CommandA,
		FExecuteAction::CreateRaw(this, &FTestYaksueModule::CommandAAction),
		FCanExecuteAction()
	);

	/**
	 * ��չ�ؿ��༭���Ĳ˵��빤����
	 */
	 //��ùؿ��༭��ģ��
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	//��չ�ؿ��༭���Ĳ˵�
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());// Extender:���������ؿ��༭���Ĳ˵�
		MenuExtender->AddMenuExtension("WindowLayout"
			, EExtensionHook::After
			, PluginCommandList
			, FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& Builder) {
				Builder.AddMenuEntry(FTestYaksueCommands::Get().CommandA);
				}));
		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}
	//��չ�ؿ��༭���Ĺ�����
	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender());// Extender:���������ؿ��༭���Ĺ�����
		ToolbarExtender->AddToolBarExtension("Settings"
			, EExtensionHook::After
			, PluginCommandList
			, FToolBarExtensionDelegate::CreateLambda([](FToolBarBuilder& Builder) {
				Builder.AddToolBarButton(FTestYaksueCommands::Get().CommandA);
				}));
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}

	//��CommandA�����ؿ��༭����GlobalLevelEditorActions�У��������Դ�����ݼ�
	/**
	 * Ҳ����˵���ؿ��༭����FUICommandList�����ڹؿ��༭���ڼ���Ƿ��п�ݼ�����֮ǰ�ڡ�ʵ����Ϊ����ӳ�����Ĳ��������Լ�������FUICommandList��û�ж���ļ���ݼ��Ƿ񴥷��Ļ��ƣ�����û���κο�ݼ�Ч��
	 */
	{
		TSharedRef<FUICommandList> LevelEditorCommandList = LevelEditorModule.GetGlobalLevelEditorActions();// ��ȡ�ؿ��༭����ȫ��UICommandList
		LevelEditorCommandList->MapAction(
			FTestYaksueCommands::Get().CommandA,//��������ʵ��
			FExecuteAction::CreateRaw(this, &FTestYaksueModule::CommandAAction),//��������Ч��
			FCanExecuteAction());
	}

	/// �й����������ģ��
	{
		//������������ģ��
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));// ���������ģ��
		//�������������������
		TArray<FContentBrowserCommandExtender>& CBCommandExtenderDelegates = ContentBrowserModule.GetAllContentBrowserCommandExtenders();// ȡ������Extender��ί��,��һ��ί��
		CBCommandExtenderDelegates.Add(FContentBrowserCommandExtender::CreateLambda([this](TSharedRef<FUICommandList> CommandList, FOnContentBrowserGetSelection GetSelectionDelegate) {
				CommandList->MapAction(FTestYaksueCommands::Get().CommandB,
					FExecuteAction::CreateLambda([this, GetSelectionDelegate] {
						CommandBAction(GetSelectionDelegate);
						})
				);
			})
		);

		/**
		* �����˵���չ
		* UE4�жദ�˵���������չ������
		*/
		// ���������������Asset�Ҽ��˵�
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBAssetMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();// �����������ģ��ȡ��һ���й�asset���Ҽ��˵�ί��
		// ע�������ί���� TSharedRef<FExtender> (const TArray<FAssetData>&)
		CBAssetMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateLambda([](const TArray<FAssetData>& SelectedAssets) {
			//��Ӳ˵���չ
			TSharedRef<FExtender> Extender(new FExtender());
			Extender->AddMenuExtension(// Ϊĳ��Extender����һ�׵����Ĳ˵�
				"AssetContextReferences",// "AssetContextReferences"ָ�������Ҫ��ӵ��ķ���λ��
				EExtensionHook::First,
				nullptr,// ��nullptr����λ�ñ���Ӧ����һ��FUICommandList����������Ϊ�ա�����ΪContentBrowserModule�Ѿ�ӵ��һ��FUICommandList�ˣ�������һ����ΪCommandBӳ���������
				FMenuExtensionDelegate::CreateLambda([](FMenuBuilder& MenuBuilder) {
					MenuBuilder.AddMenuEntry(FTestYaksueCommands::Get().CommandB);
					})
			);

			return Extender;
			})
		);
	}


	///����Asset�༭���еĲ˵�
	TArray<FAssetEditorExtender>& AssetEditorMenuExtenderDelegates = FAssetEditorToolkit::GetSharedMenuExtensibilityManager()->GetExtenderDelegates();
	AssetEditorMenuExtenderDelegates.Add(FAssetEditorExtender::CreateLambda([this](const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects) {
		//ӳ�����
		CommandList->MapAction(
			FTestYaksueCommands::Get().CommandA,
			FExecuteAction::CreateRaw(this, &FTestYaksueModule::CommandAAction),
			FCanExecuteAction());
		//��Ӳ˵���չ
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
	 * Unregister��TCommands�Ľӿڡ������������к�����������ص���Դ��ͨ����ģ��Ĺرպ����б����á�
	 */
	FTestYaksueCommands::Unregister();
}


void FTestYaksueModule::CommandAAction()
{
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString("Execute CommandA"));
}

// ���˹ؿ��༭�����������������Ҳ�����Ƶ�FUICommandList�����ڴ��������ʱ�򻹿���֪����ѡ�����Դ����Щ
void FTestYaksueModule::CommandBAction(FOnContentBrowserGetSelection GetSelectionDelegate)
{
	//��õ�ǰѡ�����Դ
	TArray<FAssetData> SelectedAssets;
	TArray<FString> SelectedPaths;
	if (GetSelectionDelegate.IsBound())
		GetSelectionDelegate.Execute(SelectedAssets, SelectedPaths);

	//Ҫ��ʾ����Ϣ��
	FString Message = "Execute CommandB:";
	for (auto ad : SelectedAssets)
		Message += ad.GetAsset()->GetName() + " ";
	//��ʾ�Ի���
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTestYaksueModule, TestYaksue)