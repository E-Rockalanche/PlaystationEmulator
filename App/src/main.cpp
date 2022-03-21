#include "App.h"

#include <Util/CommandLine.h>

#include <SDL.h>

#include <memory>

int main( int argc, char** argv )
{
	Util::CommandLine::Initialize( argc, argv );

	auto app = std::make_unique<App::App>();

	if ( !app->Initialize() )
		return 1;

	app->Run();

	app->Shutdown();

	app.reset();

	return 0;
}