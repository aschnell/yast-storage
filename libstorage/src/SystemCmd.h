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


#ifndef SYSTEM_CMD_H
#define SYSTEM_CMD_H

#include <sys/poll.h>
#include <stdio.h>

#include <string>
#include <vector>
#include <list>

using std::string;

namespace storage
{

class OutputProcessor;

class SystemCmd
    {
    public:
	enum OutputStream { IDX_STDOUT, IDX_STDERR };
	SystemCmd( const char* Command_Cv );
	SystemCmd( const string& Command_Cv );
	SystemCmd();
	virtual ~SystemCmd();
	int execute( const string& Command_Cv = "" );
	int executeBackground( const string& Command_Cv = "" );
	int executeRestricted( const string& Command_Cv,
	                       unsigned long MaxTimeSec,
			       unsigned long MaxLineOut,
			       bool& ExceedTime, bool& ExceedLines);
	void setOutputHandler( void (*Handle_f)( void *, string, bool ),
	                       void * Par_p );
	void logOutput() const;
	void setOutputProcessor( OutputProcessor * proc )
	    { output_proc = proc; }
	int select( string Reg_Cv, bool Invert_bv=false,
	            unsigned Idx_ii=IDX_STDOUT );
	const string& stderr() const { return( *getString(IDX_STDERR)); }
	const string& stdout() const { return( *getString(IDX_STDOUT)); }
	const string& cmd() const { return( _cmd ); }
        void setCmd( const string & newCmd ) { _cmd = newCmd; }
	const string* getString( unsigned Idx_ii=IDX_STDOUT ) const;
	const string* getLine( unsigned Num_iv, bool Selected_bv=false,
			       unsigned Idx_ii=IDX_STDOUT ) const;
	unsigned numLines( bool Selected_bv=false, unsigned Idx_ii=IDX_STDOUT ) const;
	void setCombine( const bool Combine_b=true );
	int retcode() const { return Ret_i; }

	int getStdout( std::vector<string> &Ret_Cr, const bool Append_bv = false ) const
	    { return placeOutput( IDX_STDOUT, Ret_Cr, Append_bv); }
	int getStderr( std::vector<string> &Ret_Cr, const bool Append_bv = false ) const
	    { return placeOutput( IDX_STDERR, Ret_Cr, Append_bv); }
	int getStdout( std::list<string> &Ret_Cr, const bool Append_bv = false ) const
	    { return placeOutput( IDX_STDOUT, Ret_Cr, Append_bv); }
	int getStderr( std::list<string> &Ret_Cr, const bool Append_bv = false ) const
	    { return placeOutput( IDX_STDERR, Ret_Cr, Append_bv); }

        void setStdinText( const string & stdinText ) { _stdinText = stdinText; }

	/**
	 * Quotes and protects a single string for shell execution.
	 */
	static string quote(const string& str);

	/**
	 * Quotes and protects every single string in the list for shell execution.
	 */
	static string quote(const std::list<string>& strs);

	static bool testmode;

    protected:

        int  placeOutput( unsigned Which_iv, std::vector<string> &Ret_Cr, const bool Append_bv ) const;
        int  placeOutput( unsigned Which_iv, std::list<string> &Ret_Cr, const bool Append_bv ) const;

	void invalidate();
	void closeOpenFds();
	int doExecute( string Cmd_Cv );
	bool doWait( bool Hang_bv, int& Ret_ir );
        void checkOutput();
        void sendStdin();
	void getUntilEOF( FILE* File_Cr, std::vector<string>& Lines_Cr,
	                  bool& NewLineSeen_br, bool Stderr_bv );
	void extractNewline( const char* Buf_ti, int Cnt_ii, bool& NewLineSeen_br,
	                     string& Text_Cr, std::vector<string>& Lines_Cr );
	void addLine( string Text_Cv, std::vector<string>& Lines_Cr );
	void init();

	mutable string Text_aC[2];
	mutable bool Valid_ab[2];
	FILE* File_aC[2];
        FILE* _childStdin;
	std::vector<string> Lines_aC[2];
	std::vector<string*> SelLines_aC[2];
        string _stdinText;
	bool NewLineSeen_ab[2];
	bool Combine_b;
	bool Background_b;
	string _cmd;
	int Ret_i;
	int Pid_i;
	void (* OutputHandler_f)( void*, string, bool );
	void *HandlerPar_p;
	OutputProcessor* output_proc;
	struct pollfd pfds[3];

    };


    inline string quote(const string& str)
    {
	return SystemCmd::quote(str);
    }

    inline string quote(const std::list<string>& strs)
    {
	return SystemCmd::quote(strs);
    }

}

#endif
