
//---------------------------------------------------------------------------
// The card, for the Over effect: a night scene with a skyline. A dark blue
// sky gradient above, snow below, and black buildings cutting the horizon --
// alpha 0 in the sky if asked, so the Alpha mask has something to keep.
// Rows are bottom-first (GL order).
//---------------------------------------------------------------------------
Floats buildCard( int width, int height, bool skyTransparent )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = ( y + 0.5 ) / height;
			const double skyline = 0.28 + 0.06 * std::floor( 6.0 * hash01( static_cast< uint32_t >( u * 23.0 ), 5 ) )
			                       * ( hash01( static_cast< uint32_t >( u * 23.0 ), 6 ) > 0.4 ? 1.0 : 0.0 );
			float* o = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			if( v < 0.22 )
			{
				//Snow, faintly blue.
				const double g = 0.55 + 0.1 * hash01( static_cast< uint32_t >( x * 7 + y * 131 ) );
				o[ 0 ] = static_cast< float >( 0.85 * g );
				o[ 1 ] = static_cast< float >( 0.88 * g );
				o[ 2 ] = static_cast< float >( 0.95 * g );
				o[ 3 ] = 1.0f;
			}
			else if( v < skyline )
			{
				//Buildings, with a few lit windows.
				const bool window = ( x / 6 ) % 3 == 1 && ( y / 8 ) % 2 == 0 && hash01( static_cast< uint32_t >( x / 6 ), static_cast< uint32_t >( y / 8 ) ) > 0.7;
				o[ 0 ] = window ? 0.9f : 0.02f;
				o[ 1 ] = window ? 0.7f : 0.02f;
				o[ 2 ] = window ? 0.35f : 0.03f;
				o[ 3 ] = 1.0f;
			}
			else
			{
				//Sky: deep blue, darker upwards.
				o[ 0 ] = static_cast< float >( 0.02 + 0.03 * ( 1.0 - v ) );
				o[ 1 ] = static_cast< float >( 0.03 + 0.05 * ( 1.0 - v ) );
				o[ 2 ] = static_cast< float >( 0.08 + 0.12 * ( 1.0 - v ) );
				o[ 3 ] = skyTransparent ? 0.0f : 1.0f;
			}
		}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

Floats readTexture( GLuint texture, int width, int height, GLenum format = GL_RGBA )
{
	const int channels = format == GL_RGBA ? 4 : ( format == GL_RG ? 2 : 1 );
	Floats data( static_cast< size_t >( width ) * height * channels );
	glBindTexture( GL_TEXTURE_2D, texture );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glGetTexImage( GL_TEXTURE_2D, 0, format, GL_FLOAT, data.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return data;
}

//---------------------------------------------------------------------------
// Audio, written into the Audio buffer the way the host writes it.
//---------------------------------------------------------------------------
enum class AudioFeed
{
	Silence,
	Pulses///< a bass-heavy spectrum with a hit every half second
};

void feedAudio( BorealPlugin& plugin, double seconds, AudioFeed feed )
{
	const double beat  = std::fmod( seconds, 0.5 );
	const float strike = feed == AudioFeed::Pulses ? static_cast< float >( 0.15 + 1.5 * std::exp( -beat / 0.06 ) ) : 0.0f;
	for( int bin = 0; bin < audio::kBins; ++bin )
	{
		const float across = static_cast< float >( bin ) / static_cast< float >( audio::kBins - 1 );
		const float shape  = 0.7f * ( 1.0f - across ) * ( 1.0f - across ) + 0.2f * ( 0.5f + 0.5f * std::sin( 25.0f * across ) );
		plugin.SetParamElementValue( PT_AUDIO, static_cast< unsigned int >( bin ), shape * strike );
	}
}

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic 60 fps clock.
//---------------------------------------------------------------------------
struct Rig
{
	BorealPlugin plugin;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame            = 0;
	double fps           = 60.0;
	double clockOffset   = 0.0;
	AudioFeed feed       = AudioFeed::Silence;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	explicit Rig( bool effect = false ) : plugin( effect )
	{
	}

	~Rig()
	{
		plugin.DeInitGL();
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		width  = w;
		height = h;
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( width );
		viewport.height             = static_cast< FFUInt32 >( height );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/boreal for which shader\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );

		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;

		process.HostFBO = outputFBO;
		if( plugin.IsEffect() )
		{
			const Floats card = picture ? *picture : buildCard( width, height, false );
			sourceTexture     = makeTexture( width, height, card.data() );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle                              = sourceTexture;
			inputs[ 0 ]                                     = &inputStruct;
			process.numInputTextures                        = 1;
			process.inputTextures                           = inputs;
		}
		return true;
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetFloatParameter( id, value );
	}

	void Press( unsigned int id )
	{
		plugin.SetFloatParameter( id, 1.0f );
		plugin.SetFloatParameter( id, 0.0f );
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			const double seconds = clockOffset + static_cast< double >( frame ) / fps;
			plugin.SetTime( seconds );
			feedAudio( plugin, seconds, feed );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	/// A clip trigger: the host's clock goes back to the start.
	void Retrigger()
	{
		clockOffset = -static_cast< double >( frame ) / fps;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	Floats March() const
	{
		return readTexture( plugin.MarchTextureID(), plugin.MarchWidth(), plugin.MarchHeight() );
	}

	Floats State() const
	{
		return readTexture( plugin.StateTextureID(), plugin.MapSize(), plugin.MapSize() );
	}
};

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	std::string kind;
};

