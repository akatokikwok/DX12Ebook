#include "TestYaksueStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr< FSlateStyleSet > FTestYaksueStyle::StyleInstance = NULL;

void FTestYaksueStyle::Initialize()
{
	if (!StyleInstance.IsValid()) {
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FTestYaksueStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FTestYaksueStyle::GetStyleSetName()
{
	return TEXT("TestYaksueStyle");
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )

const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef< FSlateStyleSet > FTestYaksueStyle::Create()
{
	//创建一个新的Style实例：
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

	//设置资源目录，为本插件的Resources目录
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("TestYaksue")->GetBaseDir() / TEXT("Resources"));

	//注册图标：
	Style->Set("TestYaksueCommands.CommandA", new IMAGE_BRUSH(TEXT("PicA"), Icon40x40));
	Style->Set("TestYaksueCommands.CommandB", new IMAGE_BRUSH(TEXT("PicB"), Icon40x40));

	return Style;
}

#undef IMAGE_BRUSH


void FTestYaksueStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}
