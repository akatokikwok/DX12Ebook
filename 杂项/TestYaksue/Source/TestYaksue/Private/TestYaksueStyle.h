#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FTestYaksueStyle
{
public:

	//初始化，启动模块时调用
	static void Initialize();

	//关闭模块时调用
	static void Shutdown();

	/** reloads textures used by slate renderer */
	static void ReloadTextures();

	//返回这个style的名字
	static FName GetStyleSetName();

private:

	//创建一个实例
	static TSharedRef< class FSlateStyleSet > Create();// TSharedRef可以直接转化为TSharedPtr

private:

	//style实例
	static TSharedPtr< class FSlateStyleSet > StyleInstance;
};
