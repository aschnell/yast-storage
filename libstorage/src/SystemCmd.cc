/*
 * Copyright (c) [2004-2009] Novell, Inc.
 *
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, contact Novell, Inc.
 *
 * To contact Novell about this file by physical or electronic mail, you may
 * find current contact information at www.novell.com.
 */

// Maintainer: fehr@suse.de
/*
  Textdomain    "storage"
*/

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <sys/wait.h>

#include <string>
#include <boost/algorithm/string/replace.hpp>

#include "y2storage/AppUtil.h"
#include "y2storage/SystemCmd.h"
#include "y2storage/OutputProcessor.h"

using namespace std;
using namespace storage;


SystemCmd::SystemCmd( const char* Command )
{
    y2mil("constructor SystemCmd:\"" << Command << "\"");
    init();
    execute( Command );
}

SystemCmd::SystemCmd( const string& Command_Cv )
{
    y2mil("constructor SystemCmd:\"" << Command_Cv << "\"");
    init();
    execute( Command_Cv );
}

SystemCmd::SystemCmd()
{
    y2mil("constructor SystemCmd");
    init();
}

void SystemCmd::init()
    {
    Combine_b = false;
    OutputHandler_f = NULL;
    HandlerPar_p = NULL;
    output_proc = NULL;
    File_aC[0] = File_aC[1] = NULL;
    _childStdin = NULL;
    pfds[0].events = POLLOUT; // stdin
    pfds[1].events = POLLIN;  // stdout
    pfds[2].events = POLLIN;  // stderr
    }

SystemCmd::~SystemCmd()
    {
    if ( _childStdin )
        fclose( _childStdin );
    if( File_aC[IDX_STDOUT] )
	fclose( File_aC[IDX_STDOUT] );
    if( File_aC[IDX_STDERR] )
	fclose( File_aC[IDX_STDERR] );
    }

void
SystemCmd::setOutputHandler( void (*Handle_f)( void *, string, bool ),
	                     void * Par_p )
    {
    OutputHandler_f = Handle_f;
    HandlerPar_p = Par_p;
    }

void
SystemCmd::closeOpenFds()
    {
    int max_fd = getdtablesize();
    for( int fd = 3; fd < max_fd; fd++ )
	{
	close(fd);
	}
    }


int
SystemCmd::execute( const string& Cmd_Cv )
{
    y2mil("SystemCmd Executing:\"" << Cmd_Cv << "\"");
    Background_b = false;
    return doExecute(Cmd_Cv);
}


int
SystemCmd::executeBackground( const string& Cmd_Cv )
{
    y2mil("SystemCmd Executing (Background):\"" << Cmd_Cv << "\"");
    Background_b = true;
    return doExecute(Cmd_Cv);
}


int
SystemCmd::executeRestricted( const string& Command_Cv,
			      long unsigned MaxTimeSec,
			      long unsigned MaxLineOut,
			      bool& ExceedTime, bool& ExceedLines )
{
    y2mil("cmd:" << Command_Cv << " MaxTime:" << MaxTimeSec << " MaxLines:" << MaxLineOut);
    ExceedTime = ExceedLines = false;
    int ret = executeBackground( Command_Cv );
    unsigned long ts = 0;
    unsigned long ls = 0;
    unsigned long start_time = time(NULL);
    while( !ExceedTime && !ExceedLines && !doWait( false, ret ) )
	{
	if( MaxTimeSec>0 )
	    {
	    ts = time(NULL)-start_time;
	    y2mil( "time used:" << ts );
	    }
	if( MaxLineOut>0 )
	    {
	    ls = numLines()+numLines(false,IDX_STDERR);
	    y2mil( "lines out:" << ls );
	    }
	ExceedTime = MaxTimeSec>0 && ts>MaxTimeSec;
	ExceedLines = MaxLineOut>0 && ls>MaxLineOut;
	sleep( 1 );
	}
    if( ExceedTime || ExceedLines )
	{
	int r = kill( Pid_i, SIGKILL );
	y2mil( "kill pid:" << Pid_i << " ret:" << r );
	unsigned count=0;
	int Status_ii;
	int Wait_ii = -1;
	while( count<5 && Wait_ii<=0 )
	    {
	    Wait_ii = waitpid( Pid_i, &Status_ii, WNOHANG );
	    y2mil( "waitpid:" << Wait_ii );
	    count++;
	    sleep( 1 );
	    }
	/*
	r = kill( Pid_i, SIGKILL );
	y2mil( "kill pid:" << Pid_i << " ret:" << r );
	count=0;
	waitDone = false;
	while( count<8 && !waitDone )
	    {
	    y2mil( "doWait:" << count );
	    waitDone = doWait( false, ret );
	    count++;
	    sleep( 1 );
	    }
	*/
	Ret_i = -257;
	}
    else
	Ret_i = ret;
    y2mil("ret:" << ret << " ExceedTime:" << ExceedTime << " ExceedLines:" << ExceedLines);
    return ret;
}


#define PRIMARY_SHELL "/bin/sh"
#define ALTERNATE_SHELL "/bin/bash"

int
SystemCmd::doExecute( string command )
    {
    string Shell_Ci = PRIMARY_SHELL;
    if( access( Shell_Ci.c_str(), X_OK ) != 0 )
	{
	Shell_Ci = ALTERNATE_SHELL;
	}

        if ( ! command.empty() )
            _cmd = command;

        if ( _cmd.empty() )
            {
            y2error( "No command specified" );
            return -1;
            }

    if( output_proc )
	{
	output_proc->reset();
	}
    y2deb("Cmd:" << _cmd);

    _childStdin = NULL;
    File_aC[IDX_STDERR] = File_aC[IDX_STDOUT] = NULL;
    invalidate();
    int sin[2];
    int sout[2];
    int serr[2];
    bool ok_bi = true;
    if( !testmode && pipe(sin)<0 )
	{
	y2error( "pipe stdin creation failed errno=%d (%s)", errno,
	         strerror(errno));
	ok_bi = false;
	}
    if( !testmode && pipe(sout)<0 )
	{
	y2error( "pipe stdout creation failed errno=%d (%s)", errno,
	         strerror(errno));
	ok_bi = false;
	}
    if( !testmode && !Combine_b && pipe(serr)<0 )
	{
	y2error( "pipe stderr creation failed errno=%d (%s)", errno,
	         strerror(errno));
	ok_bi = false;
	}
    if( !testmode && ok_bi )
	{
	pfds[0].fd = sin[1];
	if( fcntl( pfds[0].fd, F_SETFL, O_NONBLOCK )<0 )
	    {
	    y2error( "fcntl O_NONBLOCK failed errno=%d (%s)", errno,
		     strerror(errno));
	    }
	pfds[1].fd = sout[0];
	if( fcntl( pfds[1].fd, F_SETFL, O_NONBLOCK )<0 )
	    {
	    y2error( "fcntl O_NONBLOCK failed errno=%d (%s)", errno,
		     strerror(errno));
	    }
	if( !Combine_b )
	    {
	    pfds[2].fd = serr[0];
	    if( fcntl( pfds[2].fd, F_SETFL, O_NONBLOCK )<0 )
		{
		y2error( "fcntl O_NONBLOCK failed errno=%d (%s)", errno,
			 strerror(errno));
		}
	    }
	y2debug( "sout:%d serr:%d", pfds[1].fd, Combine_b?-1:pfds[2].fd );
	switch( (Pid_i=fork()) )
	    {
	    case 0:
		setenv( "LC_ALL", "C", 1 );
		setenv( "LANGUAGE", "C", 1 );
		if( dup2( sin[0], 0 )<0 )
		    {
		    y2error( "dup2 stdin child failed errno=%d (%s)", errno,
			     strerror(errno));
		    }
		if( dup2( sout[1], 1 )<0 )
		    {
		    y2error( "dup2 stdout child failed errno=%d (%s)", errno,
			     strerror(errno));
		    }
		if( !Combine_b && dup2( serr[1], 2 )<0 )
		    {
		    y2error( "dup2 stderr child failed errno=%d (%s)", errno,
			     strerror(errno));
		    }
		if( Combine_b && dup2( 1, 2 )<0 )
		    {
		    y2error( "dup2 stderr child failed errno=%d (%s)", errno,
			     strerror(errno));
		    }
		if( close( sin[1] )<0 )
		    {
		    y2error( "close child failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		if( close( sout[0] )<0 )
		    {
		    y2error( "close child failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		if( !Combine_b && close( serr[0] )<0 )
		    {
		    y2error( "close child failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		closeOpenFds();
		Ret_i = execl( Shell_Ci.c_str(), Shell_Ci.c_str(), "-c",
			       _cmd.c_str(), NULL );
		y2error( "SHOULD NOT HAPPEN \"%s\" Ret:%d", Shell_Ci.c_str(),
			 Ret_i );
		break;
	    case -1:
		Ret_i = -1;
		break;
	    default:
		if( close( sin[0] )<0 )
		    {
		    y2error( "close parent failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		if( close( sout[1] )<0 )
		    {
		    y2error( "close parent failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		if( !Combine_b && close( serr[1] )<0 )
		    {
		    y2error( "close parent failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		Ret_i = 0;
                _childStdin = fdopen( sin[1], "a" );
                if ( _childStdin == NULL )
		    {
		    y2error( "fdopen stdin failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		File_aC[IDX_STDOUT] = fdopen( sout[0], "r" );
		if( File_aC[IDX_STDOUT] == NULL )
		    {
		    y2error( "fdopen stdout failed errno=%d (%s)", errno,
		             strerror(errno));
		    }
		if( !Combine_b )
		    {
		    File_aC[IDX_STDERR] = fdopen( serr[0], "r" );
		    if( File_aC[IDX_STDERR] == NULL )
			{
			y2error( "fdopen stderr failed errno=%d (%s)", errno,
				 strerror(errno));
			}
		    }
		if( !Background_b )
		    {
		    doWait( true, Ret_i );
		    }
		break;
	    }
	}
    else if( !testmode )
	{
	Ret_i = -1;
	}
    else
	{
	Ret_i = 0;
	y2mil("TESTMODE would execute \"" << _cmd << "\"");
	}
    if( Ret_i==-127 || Ret_i==-1 )
	{
	y2err("system (\"" << _cmd << "\") = " << Ret_i);
	}
    if( !testmode )
	checkOutput();
    y2mil("system() Returns:" << Ret_i);
    if( Ret_i!=0 )
	logOutput();
    return Ret_i;
    }


bool
SystemCmd::doWait( bool Hang_bv, int& Ret_ir )
    {
    int Wait_ii;
    int Status_ii;
    int sel;

    do
	{
	y2debug( "[0] id:%d ev:%x [1] fs:%d ev:%x",
	             pfds[1].fd, (unsigned)pfds[1].events,
		     Combine_b?-1:pfds[2].fd, Combine_b?0:(unsigned)pfds[2].events );
	if( (sel=poll( pfds, Combine_b?2:3, 1000 ))<0 )
	    {
	    y2error( "poll failed errno=%d (%s)", errno, strerror(errno));
	    }
	y2debug( "poll ret:%d", sel );
	if( sel>0 )
	    {
            if ( pfds[0].revents )
                sendStdin();
            if ( pfds[1].revents || pfds[2].revents )
                checkOutput();
	    }
	Wait_ii = waitpid( Pid_i, &Status_ii, WNOHANG );
	y2debug( "Wait ret:%d", Wait_ii );
	}
    while( Hang_bv && Wait_ii == 0 );
    if( Wait_ii != 0 )
	{
	checkOutput();
        if ( _childStdin )
            {
            fclose( _childStdin );
            _childStdin = NULL;
            }
	fclose( File_aC[IDX_STDOUT] );
	File_aC[IDX_STDOUT] = NULL;
	if( !Combine_b )
	    {
	    fclose( File_aC[IDX_STDERR] );
	    File_aC[IDX_STDERR] = NULL;
	    }
	if( !WIFEXITED(Status_ii) )
	    {
	    Ret_ir = -127;
	    }
	else
	    {
	    Ret_ir = WEXITSTATUS(Status_ii);
	    }
	if( output_proc )
	    {
	    output_proc->finished();
	    }
	}
    y2debug( "Wait:%d pid=%d stat=%d Hang:%d Ret:%d", Wait_ii, Pid_i,
             Status_ii, Hang_bv, Ret_ir );
    return Wait_ii != 0;
    }

void
SystemCmd::setCombine( const bool Comb_bv )
    {
    Combine_b = Comb_bv;
    }

const string *
SystemCmd::getString( unsigned Idx_iv ) const
    {
    if( Idx_iv > 1 )
	{
	y2err("invalid index " << Idx_iv);
	}
    if( !Valid_ab[Idx_iv] )
	{
	unsigned int I_ii;

	Text_aC[Idx_iv] = "";
	I_ii=0;
	while( I_ii<Lines_aC[Idx_iv].size() )
	    {
	    Text_aC[Idx_iv] += Lines_aC[Idx_iv][I_ii];
	    Text_aC[Idx_iv] += '\n';
	    I_ii++;
	    }
	Valid_ab[Idx_iv] = true;
	}
    return &Text_aC[Idx_iv];
    }

unsigned
SystemCmd::numLines( bool Sel_bv, unsigned Idx_iv ) const
    {
    unsigned Ret_ii;

    if( Idx_iv > 1 )
	{
	y2err("invalid index " << Idx_iv);
	}
    if( Sel_bv )
	{
	Ret_ii = SelLines_aC[Idx_iv].size();
	}
    else
	{
	Ret_ii = Lines_aC[Idx_iv].size();
	}
    y2deb("ret:" << Ret_ii);
    return Ret_ii;
    }

const string *
SystemCmd::getLine( unsigned Nr_iv, bool Sel_bv, unsigned Idx_iv ) const
    {
    const string * Ret_pCi = NULL;

    if( Idx_iv > 1 )
	{
	y2err("invalid index " << Idx_iv);
	}
    if( Sel_bv )
	{
	if( Nr_iv < SelLines_aC[Idx_iv].capacity() )
	    {
	    Ret_pCi = SelLines_aC[Idx_iv][Nr_iv];
	    }
	}
    else
	{
	if( Nr_iv < Lines_aC[Idx_iv].size() )
	    {
	    Ret_pCi = &Lines_aC[Idx_iv][Nr_iv];
	    }
	}
    return Ret_pCi;
    }

int
SystemCmd::select( string Pat_Cv, bool Invert_bv, unsigned Idx_iv )
    {
    int I_ii;
    int End_ii;
    int Size_ii;
    string::size_type Pos_ii;
    bool BeginOfLine_bi;
    string Search_Ci( Pat_Cv );

    if( Idx_iv > 1 )
	{
	y2err("invalid index " << Idx_iv);
	}
    BeginOfLine_bi = Search_Ci.length()>0 && Search_Ci[0]=='^';
    if( BeginOfLine_bi )
	{
	Search_Ci.erase( 0, 1 );
	}
    SelLines_aC[Idx_iv].resize(0);
    Size_ii = 0;
    End_ii = Lines_aC[Idx_iv].size();
    for( I_ii=0; I_ii<End_ii; I_ii++ )
	{
	Pos_ii = Lines_aC[Idx_iv][I_ii].find( Search_Ci );
	if( Pos_ii>0 && BeginOfLine_bi )
	    {
	    Pos_ii = string::npos;
	    }
	if( (Pos_ii != string::npos) != Invert_bv )
	    {
	    SelLines_aC[Idx_iv].resize( Size_ii+1 );
	    SelLines_aC[Idx_iv][Size_ii] = &Lines_aC[Idx_iv][I_ii];
	    y2debug( "Select Added Line %d \"%s\"", Size_ii,
		     SelLines_aC[Idx_iv][Size_ii]->c_str() );
	    Size_ii++;
	    }
	}
    y2milestone( "Pid:%d Idx:%d Pattern:\"%s\" Invert:%d Lines %d", Pid_i,
                 Idx_iv, Pat_Cv.c_str(), Invert_bv, Size_ii );
    return Size_ii;
    }

void
SystemCmd::invalidate()
    {
    int Idx_ii;

    for( Idx_ii=0; Idx_ii<2; Idx_ii++ )
	{
	Valid_ab[Idx_ii] = false;
	SelLines_aC[Idx_ii].resize(0);
	Lines_aC[Idx_ii].clear();
	NewLineSeen_ab[Idx_ii] = true;
	}
    }

void
SystemCmd::checkOutput()
    {
    y2debug( "NewLine out:%d err:%d", NewLineSeen_ab[IDX_STDOUT],
	     NewLineSeen_ab[IDX_STDERR] );
    if( File_aC[IDX_STDOUT] )
	getUntilEOF( File_aC[IDX_STDOUT], Lines_aC[IDX_STDOUT],
		     NewLineSeen_ab[IDX_STDOUT], false );
    if( File_aC[IDX_STDERR] )
	getUntilEOF( File_aC[IDX_STDERR], Lines_aC[IDX_STDERR],
		     NewLineSeen_ab[IDX_STDERR], true );
    y2debug( "NewLine out:%d err:%d", NewLineSeen_ab[IDX_STDOUT],
	     NewLineSeen_ab[IDX_STDERR] );
    }

    void
    SystemCmd::sendStdin()
    {
    if ( ! _childStdin )
        return;

    if ( ! _stdinText.empty() )
        {
        string::size_type count = 0;
        string::size_type len   = _stdinText.size();
        int result = 1;

        while ( count < len && result > 0 )
            result = fputc( _stdinText[ count++ ], _childStdin );

        _stdinText.erase( 0, count );
        }

    if ( _stdinText.empty() )
        {
        fclose( _childStdin );
        _childStdin = NULL;
        pfds[0].fd = -1; // ignore for poll() from now on
        }
    }


#define BUF_LEN 256

void
SystemCmd::getUntilEOF( FILE* File_Cr, vector<string>& Lines_Cr,
                        bool& NewLine_br, bool Stderr_bv )
    {
    size_t old_size = Lines_Cr.size();
    char Buf_ti[BUF_LEN];
    int Cnt_ii;
    int Char_ii;
    string Text_Ci;

    clearerr( File_Cr );
    Cnt_ii = 0;
    Char_ii = EOF;
    while( (Char_ii=fgetc(File_Cr)) != EOF )
	{
	Buf_ti[Cnt_ii++] = Char_ii;
	if( Cnt_ii==sizeof(Buf_ti)-1 )
	    {
	    Buf_ti[Cnt_ii] = 0;
	    extractNewline( Buf_ti, Cnt_ii, NewLine_br, Text_Ci, Lines_Cr );
	    Cnt_ii = 0;
	    if( output_proc )
		{
		output_proc->process( Buf_ti, Stderr_bv );
		}
	    if( OutputHandler_f )
		{
		y2deb("Calling Output-Handler Buf:\"" << Buf_ti << "\" Stderr:" << Stderr_bv);
		OutputHandler_f( HandlerPar_p, Buf_ti, Stderr_bv );
		}
	    }
	Char_ii = EOF;
	}
    if( Cnt_ii>0 )
	{
	Buf_ti[Cnt_ii] = 0;
	extractNewline( Buf_ti, Cnt_ii, NewLine_br, Text_Ci, Lines_Cr );
	if( output_proc )
	    {
	    output_proc->process( Buf_ti, Stderr_bv );
	    }
	if( OutputHandler_f )
	    {
	    y2deb("Calling Output-Handler Buf:\"" << Buf_ti << "\" Stderr:" << Stderr_bv);
	    OutputHandler_f( HandlerPar_p, Buf_ti, Stderr_bv );
	    }
	}
    if( Text_Ci.length() > 0 )
	{
	if( NewLine_br )
	    {
	    addLine( Text_Ci, Lines_Cr );
	    }
	else
	    {
	    Lines_Cr[Lines_Cr.size()-1] += Text_Ci;
	    }
	NewLine_br = false;
	}
    else
	{
	NewLine_br = true;
	}
    y2deb("Text_Ci:" << Text_Ci << " NewLine:" << NewLine_br);
    if( old_size != Lines_Cr.size() )
	{
	y2mil("pid:" << Pid_i << " added lines:" << Lines_Cr.size() - old_size << " stderr:" << Stderr_bv);
	}
    }

void
SystemCmd::extractNewline( const char* Buf_ti, int Cnt_iv, bool& NewLine_br,
                           string& Text_Cr, vector<string>& Lines_Cr )
    {
    string::size_type Idx_ii;

    Text_Cr += Buf_ti;
    while( (Idx_ii=Text_Cr.find( '\n' )) != string::npos )
	{
	if( !NewLine_br )
	    {
	    Lines_Cr[Lines_Cr.size()-1] += Text_Cr.substr( 0, Idx_ii );
	    }
	else
	    {
	    addLine( Text_Cr.substr( 0, Idx_ii ), Lines_Cr );
	    }
	Text_Cr.erase( 0, Idx_ii+1 );
	NewLine_br = true;
	}
    y2debug( "Text_Ci:%s NewLine:%d", Text_Cr.c_str(), NewLine_br );
    }


void
SystemCmd::addLine(string Text_Cv, vector<string>& Lines_Cr)
{
    if (Lines_Cr.size() < 100)
    {
	y2mil("Adding Line " << Lines_Cr.size() + 1 << " \"" << Text_Cv << "\"");
    }
    else
    {
	y2deb("Adding Line " << Lines_Cr.size() + 1 << " \"" << Text_Cv << "\"");
    }
    
    Lines_Cr.push_back(Text_Cv);
}


void
SystemCmd::logOutput() const
{
    for (unsigned i = 0; i < numLines(false, IDX_STDERR); ++i)
	y2mil("stderr:" << *getLine(i, false, IDX_STDERR));
    for (unsigned i = 0; i < numLines(false, IDX_STDOUT); ++i)
	y2mil("stdout:" << *getLine(i, false, IDX_STDOUT));
}


///////////////////////////////////////////////////////////////////
//
//	METHOD NAME : SystemCmd::PlaceOutput
//	METHOD TYPE : int
//
//	INPUT  :
//	OUTPUT :
//	DESCRIPTION : Place stdout/stderr linewise in Ret_Cr
//
int SystemCmd::placeOutput( unsigned Which_iv, vector<string> &Ret_Cr,
                            const bool Append_bv ) const
{
  if ( !Append_bv )
    Ret_Cr.clear();

  int Lines_ii = numLines( false, Which_iv );

  for ( int i_ii = 0; i_ii < Lines_ii; i_ii++ )
    Ret_Cr.push_back( *getLine( i_ii, false, Which_iv ) );

  return Lines_ii;
}

int SystemCmd::placeOutput( unsigned Which_iv, list<string> &Ret_Cr,
                            const bool Append_bv ) const
{
  if ( !Append_bv )
    Ret_Cr.clear();

  int Lines_ii = numLines( false, Which_iv );

  for ( int i_ii = 0; i_ii < Lines_ii; i_ii++ )
    Ret_Cr.push_back( *getLine( i_ii, false, Which_iv ) );

  return Lines_ii;
}


string SystemCmd::quote(const string& str)
{
    return "'" + boost::replace_all_copy(str, "'", "'\\''") + "'";
}


string SystemCmd::quote(const list<string>& strs)
{
    string ret;
    for (std::list<string>::const_iterator it = strs.begin(); it != strs.end(); it++)
    {
	if (it != strs.begin())
	    ret.append(" ");
	ret.append(quote(*it));
    }
    return ret;
}


bool SystemCmd::testmode = false;
