#pragma once

#include <boost/locale.hpp>
#include <boost/locale/encoding.hpp>

namespace Utility 
{
	template <typename T>
	std::wstring to_utf( const T& t, std::string enc )
	{
		using boost::locale::conv::to_utf;
		return to_utf<wchar_t>(
			std::string( (char*)(t) ), enc );
	}

	std::wstring to_utf( const std::string& t, std::string enc );

	template <typename T>
	std::wstring sjis_to_utf( const T& t )
	{
		return to_utf( t, "shift-jis" );
	}

	template <typename T>
	std::string read_default( const T& t )
	{
		return std::string( (char*)(t) );
	}

	template <typename T>
	std::wstring ascii_to_utf( const T& t )
	{
		std::string text( (char*)(t) );
		return std::wstring( text.begin(), text.end() );
	}
}
