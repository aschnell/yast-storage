#include <sstream>

#include <ycp/y2log.h>

#include "y2storage/Volume.h"
#include "y2storage/Container.h"

Volume::Volume( const Container& d, unsigned PNr, unsigned long long SizeK ) 
    : cont(&d), deleted(false)
    {
    numeric = true;
    nr = PNr;
    size_k = SizeK;
    Init();
    y2milestone( "constructed volume %s on disk %s", dev.c_str(),
                 cont->Name().c_str() );
    }

Volume::Volume( const Container& c, const string& Name, unsigned long long SizeK ) : cont(&c)
    {
    numeric = false;
    name = Name;
    size_k = SizeK;
    Init();
    y2milestone( "constructed volume \"%s\" on disk %s", dev.c_str(),
                 cont->Name().c_str() );
    }

Volume::~Volume()
    {
    y2milestone( "destructed volume %s", dev.c_str() );
    }

void Volume::Init()
    {
    major = minor = 0;
    deleted = false;
    std::ostringstream Buf_Ci;
    if( numeric )
	Buf_Ci << "/dev/" << cont->Name() << nr;
    else
	Buf_Ci << "/dev/" << cont->Name() << "/" << name;
    dev = Buf_Ci.str();
    if( numeric )
	{
	Buf_Ci.str("");
	Buf_Ci << cont->Name() << nr;
	name = Buf_Ci.str();
	}
    else
	nr = 0;
    }

bool Volume::operator== ( const Volume& rhs ) const
    {
    return( (*cont)==(*rhs.cont) && 
            name == rhs.name && 
	    deleted == rhs.deleted ); 
    }

bool Volume::operator< ( const Volume& rhs ) const
    {
    if( *cont != *rhs.cont )
	return( *cont<*rhs.cont );
    else if( name != rhs.name )
	{
	if( numeric )
	    return( nr<rhs.nr );
	else
	    return( name<rhs.name );
	}
    else
	return( !deleted );
    }



