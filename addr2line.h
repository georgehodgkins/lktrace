/* addr2line.c -- convert addresses to line number and function name

   This file was part of GNU Binutils until a lazy programmer stole it
   because it could find symbol names better than their own solution.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program (./LICENSE); if not, write to the Free Software
   Foundation, 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */


/* Derived from objdump.c and nm.c by Ulrich.Lauther@mchp.siemens.de */

/* Significantly modified for use in the lktrace utility as a library call:
 * - we C++ now (I like std::string)
 * - brought in dladdr() so we can perform a lookup based on runtime address alone
 * - removed file error checking (files returned from dladdr() are all valid binaries)
 * - cache opened files & symtabs (programs should call addr2line_cache_cleanup() when done)
 * --GH, Nov. 2020
 * */

#pragma once
#include <cassert>
#include <cstdlib>
#include <string>
#include <unordered_map>

#include <bfd.h>
#include <getopt.h>
#include <sstream>
#include <dlfcn.h>

/* Flags passed to the name demangler.  */
//#define DEMANGLE_FLAGS DMGL_PARAMS | DMGL_ANSI;
#define DEMANGLE_FLAGS 0

namespace lktrace {

// cached opened files and their symtabs
typedef std::unordered_map<std::string, std::pair<bfd*, asymbol**>> bfd_cache;
bfd_cache open_files;

// TODO: proper error handling
void* xmalloc (size_t a) {
	void* r = malloc(a);
	assert(r);
	return r;
}

// they should add a format option to to_string
std::string to_hex_string(unsigned int x) {
	std::ostringstream s;
	s << std::hex << x;
	return s.str();
}

/* Read in the symbol table.  */
asymbol** slurp_symtab (bfd *abfd)
{
	asymbol** syms;
  long storage;
  long symcount;
  bfd_boolean dynamic = FALSE;

  if ((bfd_get_file_flags (abfd) & HAS_SYMS) == 0)
    return NULL;

  storage = bfd_get_symtab_upper_bound (abfd);
  if (storage == 0)
    {
      storage = bfd_get_dynamic_symtab_upper_bound (abfd);
      dynamic = TRUE;
    }

  syms = (asymbol **) xmalloc (storage);
  if (dynamic)
    symcount = bfd_canonicalize_dynamic_symtab (abfd, syms);
  else
    symcount = bfd_canonicalize_symtab (abfd, syms);

  /* If there are no symbols left after canonicalization and
     we have not tried the dynamic symbols then give them a go.  */
  if (symcount == 0
      && ! dynamic
      && (storage = bfd_get_dynamic_symtab_upper_bound (abfd)) > 0)
    {
      free (syms);
      syms = (asymbol**) xmalloc (storage);
      symcount = bfd_canonicalize_dynamic_symtab (abfd, syms);
    }

  /* PR 17512: file: 2a1d3b5b.
     Do not pretend that we have some symbols when we don't.  */
  if (symcount <= 0)
    {
      free (syms);
      syms = NULL;
    }

  return syms;
}

// translate an address into fn_name@file_name:line_number
std::string translate_address (bfd *abfd, bfd_vma pc, asymbol** symtab)
{
      /*if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
	{
	  const struct elf_backend_data *bed = get_elf_backend_data (abfd);
	  bfd_vma sign = (bfd_vma) 1 << (bed->s->arch_size - 1);

	  pc &= (sign << 1) - 1;
	  if (bed->sign_extend_vma)
	    pc = (pc ^ sign) - sign;
	}*/ // this block depends on internal libbfd functions we don't have


      std::string rtn = "";
      const char* fn_name;
      const char* srcfile_name;
      unsigned int line;
      unsigned int discriminator;


      // find section address belongs to
	bfd_boolean found = FALSE;
	for (asection *s = abfd->sections; s != NULL; s = s->next) {

		if (bfd_section_flags(s) & SEC_ALLOC) {
			bfd_vma sxn_start = bfd_section_vma(s);
			if (pc > sxn_start && pc < sxn_start + bfd_section_size(s)) {

			  found = bfd_find_nearest_line_discriminator (abfd, s, symtab,
				pc - sxn_start, &srcfile_name, &fn_name,
				&line, &discriminator);
			  break;
			}
		}
	}

	// output format is fn_name@srcfile_name:line_discriminator
	// or empty string if not found
	if (found) {
		// parse function name, demangle if necessary
		if (!fn_name || *fn_name == '\0') rtn += "??";
		else {
			char* demang = bfd_demangle(abfd, fn_name, DEMANGLE_FLAGS);
			if (demang) {
				rtn += demang;
				free(demang);
			} else {rtn += fn_name;}
		}
		rtn += '@';

		// strip path off of filename if present
		// TODO: provide full path, let the parser strip it
		if (srcfile_name) {
			const char* h = strrchr (srcfile_name, '/');
			if (h != NULL) rtn += (h+1);
			else rtn += srcfile_name;
		
			rtn += ':';

			// line number
			if (line == 0) rtn += "??";
			else {
				rtn += to_string(line);
				if (discriminator) {
					rtn += "_";
					rtn += to_string(discriminator);
				}
			}
		}
	}

	return rtn;
}

bfd_cache::iterator open_objfile (const char* fname) {
		bfd* abfd = bfd_openr (fname, NULL);
		assert(abfd != NULL);

		abfd->flags |= BFD_DECOMPRESS;
		// this call is required (sets format flags on the file handle)
		// even though we can already be pretty sure the object is of this format
		if (!bfd_check_format(abfd, bfd_object)) {
		  assert(false);
		  return open_files.end();
		}

		asymbol** symtab = slurp_symtab (abfd);
		assert(symtab != NULL);

		auto emplit = open_files.insert(std::make_pair(std::string(fname),
			std::make_pair(abfd, symtab)));
		assert(emplit.second);
		return emplit.first;
}

std::string addr2line (const size_t addr)
{
	Dl_info info;
	int e = dladdr((void*) addr, &info);
	assert(e != 0);
	
	bfd* abfd;
	asymbol** symtab;
	auto f_it = open_files.find(std::string(info.dli_fname));
	if (f_it == open_files.end()) f_it = open_objfile(info.dli_fname);
	assert(f_it != open_files.end());

	abfd = f_it->second.first;
	symtab = f_it->second.second;

	bfd_vma file_addr = (bfd_vma) (addr - (size_t) info.dli_fbase);

	std::string rtn = translate_address (abfd, file_addr, symtab);

	if (rtn.empty() || rtn.back() == '@') { // not found, or source info not found
		// fall back to providing file name and offset
		rtn += info.dli_fname;
		rtn += "+0x";
		rtn += to_hex_string((size_t) file_addr);
	}

	return rtn;

}

// close all opened file handles and free allocated symtabs
void addr2line_cache_cleanup() {
	for (auto it = open_files.begin(); it != open_files.end(); ++it) {
		free(it->second.second);
		bfd_close(it->second.first);
	}
	open_files.clear();
}

// find the in-mem boundaries of the binary containing the given address
void find_obj_bounds (const void* addr, size_t& start, size_t& end) {
	assert(addr);
	Dl_info info;
	int e = dladdr(addr, &info);
	assert(e != 0);
	start = (size_t) info.dli_fbase;
	bfd* abfd = bfd_openr(info.dli_fname, NULL);
	abfd->flags |= BFD_DECOMPRESS;
	// can't assert directly on the condition because 
	// the function initializes necessary flags
	if (!bfd_check_format(abfd, bfd_object)) assert (false);
	end = start +
		(size_t) bfd_get_size(abfd);
	bfd_close(abfd);
}

// like dlsym, but searches .symtab instead of .dynsym
void* statsym(const char* name, void* hint) {
	assert(name && hint);
	Dl_info info;
	int e = dladdr(hint, &info);
	assert(e != 0);
	auto f_it = open_files.find(std::string(info.dli_fname));
	if (f_it == open_files.end()) f_it = open_objfile(info.dli_fname);
	assert(f_it != open_files.end());

	asymbol** symtab = f_it->second.second;
	size_t offset = 0;
	for (int a = 0; symtab[a] != NULL; ++a) {
		if (strcmp(name, symtab[a]->name) == 0) {
			offset = (size_t) symtab[a]->value;
			break;
		}
	}
	if (offset == 0) return NULL;
	else return (void*) (offset + (size_t) info.dli_fbase);
}

} // namespace lktrace

