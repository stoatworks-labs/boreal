} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	std::string outPath = "/tmp/boreal.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = 120;
	std::vector< int > substormFrames;
	bool beat = false, effect = false, transparentSky = false;
	std::string mode, scriptPath;
	int filmFrames = -1;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "brtest -- render Boreal offline and measure its sky\n\n"
			             "  --out PATH        render and write a PNG (default /tmp/boreal.png)\n"
			             "  --over            the Over effect, on the harness's night-scene card\n"
			             "  --transparent     the card's sky is alpha 0 (for Sky Mask = Alpha)\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames of 60 fps before reading back (default 120)\n"
			             "  --substorm N      press Substorm on frame N. Repeatable.\n"
			             "  --beat            feed a beat every half second into the Audio buffer\n"
			             "  --set \"Name=V\"    set a parameter by its display name. Repeatable.\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA frames in (Over) and out\n"
			             "  --film N          N frames, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n\n"
			             "  checks: --kh --invariants --knight --deposition --quench --lifetime --colour\n"
			             "          --corona --vanrhijn --extinction --over-check --determinism --onset\n"
			             "          --defaults --names --state --negative --mutation-probe --bench --engine\n" );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--substorm" && hasNext )
			substormFrames.push_back( std::atoi( argv[ ++i ] ) );
		else if( argument == "--beat" )
			beat = true;
		else if( argument == "--over" )
			effect = true;
		else if( argument == "--transparent" )
			transparentSky = true;
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "pipe";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}

	if( mode == "list" )
	{
		BorealPlugin plugin( effect );
		std::printf( "%-3s %-18s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-18s %-9s %.4f\n", parameter.index, parameter.name.c_str(), parameter.kind.c_str(),
			             parameter.value );
		return 0;
	}

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "debug-shapes" )
	{
		Rig rig;
		rig.Init( 320, 180 );
		rig.Render( 1 );
		GLint w = 0, h = 0, f = 0;
		glBindTexture( GL_TEXTURE_2D, rig.plugin.ShapeTextureID() );
		glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w );
		glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h );
		glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &f );
		GLint minf = 0; glGetTexParameteriv( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minf );
		std::printf( "shape %d x %d fmt %x min %x\n", w, h, f, minf );
		glBindTexture( GL_TEXTURE_2D, rig.plugin.ColumnTextureID() );
		glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w );
		glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h );
		glGetTexParameteriv( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minf );
		std::printf( "column %d x %d min %x\n", w, h, minf );
	}
	else if( mode == "pipe" )
		result = runPipe( effect, width, height, scriptPath, filmFrames, beat, settings );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" )
		result = runBench();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig( effect );
		const Floats card = buildCard( width, height, transparentSky );
		if( !rig.Init( width, height, &card ) )
			result = 1;
		else
		{
			for( const std::string& setting : settings )
			{
				std::string error;
				if( !applySetting( rig.plugin, setting, error ) )
				{
					std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
					return 2;
				}
			}
			if( beat )
				rig.feed = AudioFeed::Pulses;
			for( int f = 0; f < std::max( frames, 1 ) && result == 0; ++f )
			{
				if( std::find( substormFrames.begin(), substormFrames.end(), f ) != substormFrames.end() )
					rig.Press( PT_SUBSTORM );
				if( !rig.Render( 1 ) )
					result = 1;
			}
			if( result == 0 )
			{
				if( writePng( outPath, width, height, rig.Output() ) )
					std::printf( "wrote %s -- %dx%d, %d frames (%.1f s of sky, %d nodes, engine %.2f ms)\n",
					             outPath.c_str(), width, height, frames, rig.plugin.SkyTime(),
					             rig.plugin.LastSnapshot().count, rig.plugin.EngineMs() );
				else
					result = 1;
			}
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
