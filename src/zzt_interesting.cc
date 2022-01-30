// Functions for determining if data is interesting in a ZZT context.
// This is ported from Python with some minor changes.
// TODO: Make keyword() for text and HTML behave like in Python.
// Also TODO: Fix the two time sinks, and implement zip unpacking behavior.
// And also also TODO? Return priority numbers as well as the string so that
// lesser interest (e.g. keyword) is overridden by greater (.ZZT file)?

// TODO: Fix illegal reads detected by valgrind.

#pragma once

#include <algorithm>
#include <iostream>
#include <iterator>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <magic.h>	// For mime-type inference

#include <archive_entry.h>	// Recursive search inside ZIP files
#include <archive.h>		// Ditto

#ifdef USE_LIBXML
#include <libxml/HTMLparser.h> // HTML parsing (TODO: XML)
#endif

struct zzt_header {
	int16_t magic;
	uint16_t boards;
};

struct brd_header {
	int16_t board_size;
	int8_t title_length;
	char title[50];
};

const int ZZT_HEADER_MAGIC = -1, SUPERZZT_HEADER_MAGIC = -2;

// Determine various types of magic information: mime encoding and type.
// Only looks at the first 1K for speed.
std::string get_magic(const std::vector<char> & contents_bytes, int type) {
	magic_t magic;

	magic = magic_open(type);
	magic_load(magic, NULL);

	// Only looking at 1K max gives us a pretty substantial speed increase.
	const char * magic_type_chr = magic_buffer(magic, contents_bytes.data(),
		std::min(contents_bytes.size(), (size_t)1024));
	std::string magic_type;

	if (magic_type_chr) {
		magic_type = magic_type_chr;
	} else {
		// Libmagic returned an error. Throw an appropriate exception (note that
		// there's a bug in libmagic that may make the error string NULL even if
		// an error was reported.)
		std::string error_msg;
		if (magic_error(magic)) {
			error_msg = magic_error(magic);
		} else {
			error_msg = "Known bug in libmagic";
		}
		throw std::logic_error("Libmagic: " + error_msg);
	}

	magic_close(magic);

	return magic_type;
}

std::string get_magic_mimetype(const std::vector<char> & contents_bytes) {
	try {
		return get_magic(contents_bytes, MAGIC_MIME_TYPE);
	} catch (std::logic_error & e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
		return "application/unknown";
	}
}

std::string get_magic_encoding(const std::vector<char> & contents_bytes) {
	try {
		return get_magic(contents_bytes, MAGIC_MIME_ENCODING);
	} catch (std::logic_error & e) {
		std::cerr << "ERROR: " << e.what() << std::endl;
		return "application/unknown";
	}
}

std::vector<char> substr(const std::vector<char> & str, int start, int end) {
	return std::vector<char>(str.begin()+start, str.begin()+end);
}

std::vector<char> byte_seq(const char * what, int length) {
	return std::vector<char>(what, what+length);
}

std::vector<char> vecs(std::string what) {
	return std::vector<char>(what.begin(), what.end());
}

// Return a lowercased version of a string or substring.
std::string lower(std::string::const_iterator start, std::string::const_iterator stop) {
	std::string out;

	std::transform(start, stop, std::back_inserter(out),
		[](unsigned char c){ return std::tolower(c); });

	return out;
}

std::string to_str(const std::vector<char> & in) {
	return std::string(in.data(), in.data()+in.size());
}

// Produces a printable version of some arbitrary iterable structure, with
// printable letters preserved, newlines turned into spaces, and everything
// else turned into _.

// Do not use on very large texts; it's slow.

char printable_transform(char in) {
	if(in > 0x1F && in < 0x7F) { // printable letters; faster than isprint
		return in;
	}
	if (in == '\n' || in == '\r') {
		return ' ';
	}

	return '_';
}

template<typename IterIn> std::string printable(IterIn start, IterIn stop) {
	std::string out(start, stop);

	for (auto pos = out.begin(); pos != out.end(); ++pos) {
		*pos = printable_transform(*pos);
	}

	return out;
}

std::string lower(const std::string & instr) {
	return lower(instr.begin(), instr.end());
}

bool str_contains(const std::string & haystack,
	const std::vector<std::string> & needles) {

	for (const std::string & needle: needles) {
		if (haystack.find(needle) != std::string::npos) {
			return true;
		}
	}

	return false;
}

bool str_contains(const std::string & haystack, std::string needle) {
	return haystack.find(needle) != std::string::npos;
}

// Given and index into a string corresponding to the start and end of a substring
// match, this returns the window number of characters to the left of this region.
// It also returns the whole word if there's a word (i.e. sequence bracketed by
// spaces) within the window. In addition, it runs the whole thing through
// a printable transform so that the output can be printed to screen.
std::string get_surrounding_text(const std::string & text,
	size_t start_pos, size_t stop_pos, size_t window, bool ellipses) {

	// Search for spaces
	bool found_space_fwd = false, found_space_bkwd = false;
	// the +1 is so that we handle if the search term itself
	// has a space. TODO: Deal more robustly with it (what if there
	// are multiple spaces in front?)
	auto excerpt_start = text.begin() + start_pos + 1,
		excerpt_stop = text.begin() + stop_pos;

	for (size_t i = 0; i < window && excerpt_stop != text.end() &&
		!found_space_fwd; ++i) {
		// Check if the next printable-transformed char is a space. If so, stop.
		if (printable_transform(*excerpt_stop) == ' ') {
			found_space_fwd = true;
		} else {
			++excerpt_stop;
		}
	}

	found_space_bkwd = false;
	for (size_t i = 0; i < window && excerpt_start != text.begin() &&
		!found_space_bkwd; ++i) {

		if (printable_transform(*(excerpt_start-1)) == ' ') {
			found_space_bkwd=true;
		} else {
			--excerpt_start;
		}
	}

	std::string excerpt = printable(excerpt_start, excerpt_stop);

	// Denote by ellipses that the match is only part of a word, if
	// ellipses are desired and the match is incomplete.
	if (ellipses && !found_space_bkwd && excerpt_start != text.begin()) {
		excerpt = "..." + excerpt;
	}
	if (ellipses && !found_space_fwd && excerpt_stop != text.end()) {
		excerpt += "...";
	}

	return excerpt;
}

// https://stackoverflow.com/questions/4553012
bool is_file(std::string filename) {
	struct stat path_stat;
	stat(filename.data(), &path_stat);
	return S_ISREG(path_stat.st_mode);
}


// Note: this will choke with extremely large files! I don't know how to
// deal with them yet.

std::vector<char> file_to_vector(std::string filename) {
	if (!is_file(filename)) {
		throw std::runtime_error("file_to_vector: not a regular file: "
			+ filename);
	}

	std::ifstream in_file(filename, std::ios::binary);

	if (!in_file || in_file.fail()) {
		throw std::runtime_error("file_to_vector: can't open " + filename + ": "
			+ strerror(errno));
	}

	// Get the size of the file.
	std::streampos file_size;

	in_file.seekg(0, std::ios::end);
	file_size = in_file.tellg();
	in_file.seekg(0, std::ios::beg);

	// And then read the data.
	std::vector<char> file_contents(file_size);
	in_file.read(file_contents.data(), file_size);
	return file_contents;
}

// This function returns the empty string if the file is neither a ZZT nor
// Super ZZT file, "zzt" if it's a ZZT file, and "szt" if it's a Super ZZT file.
std::string zzt_szt_check(const std::vector<char> & contents_bytes,
	bool multiple_boards_reqd) {

	// ZZT identification is pretty easy. The file starts with either FF FF or
	// FE FF. Then's the number of boards, which can't be above 100, so hex
	// values ?? 00.

	// We try to check as few things as possible because a false negative is
	// worse than a false positive. We do check for a bunch of known false
	// positives, though.

	// XXX: Beware LSB/MSB stuff! This code is not endian-portable!

	if (contents_bytes.size() < 4) { return ""; }

	zzt_header possible_header;
	memcpy(&possible_header, contents_bytes.data(), sizeof(zzt_header));

	// The header's OK, and, although 100 is the hard limit on boards, I think
	// KevEdit can break this limit, so let's use 255.
	bool header_OK = (possible_header.magic == ZZT_HEADER_MAGIC ||
			possible_header.magic == SUPERZZT_HEADER_MAGIC) &&
		possible_header.boards < 255;

	if (multiple_boards_reqd) {
		header_OK &= possible_header.boards > 0;
	}

	if (!header_OK) {
		return "";
	}

	/* Handle some false positives: Creatures files are FF FF xx xx 08 00 b"Creature".
	   A ZZT file with only two boards *could* technically have this header, but the
	   player would have 29251 gems, so I think I can do this... */

	if (contents_bytes.size() >= 14) {
		// Test for a Creatures sequence. This is rather more complex than the
		// Python syntax...
		if (possible_header.magic == -1 && substr(contents_bytes, 4, 14) ==
			byte_seq("\x08" "\x00" "Creature", 10)) {
			return "";
		}
	}

	// Tie Fighter mission files (.TIE) have headers like this:
	//    Index   Type            TIE             ZZT             SZT
	//    0x00    INT16LE         -1              -1              -2
	//    0x02    INT16LE         NumFGs          NumBoards
	//    0x04    INT16LE         NumMessages     PlayerAmmo
	//    0x06    INT16LE         Reserved        PlayerGems
	//    0x08    char[7]                         PlayerKeys
	//    0x08    UINT8           Unknown1
	//    0x09    BOOL            Unknown2
	//    0x0A    BYTE            BriefingOff.
	//    0x0D    BOOL            CapturedOnEject
	//    0x0F    INT16LE                         PlayerHealth
	//    0x11    INT16LE                         PlayerBoard
	//    0x13    INT16LE                         PlayerTorches   Unused
	//    0x15    INT16LE                         TorchCycles     PlayerScore
	//    0x17    INT16LE                         EnergyCycles    Unused
	//    0x18    char[64][6]     EndMissionMessages
	//    0x19    INT16LE                         Unused          EnergyCycles
	//    0x1B    UINT8                                           WorldNameLen
	//    0x1B    INT16LE                         PlayerScore

	// http://www.empirereborn.net/subdomains/idmr/Resources//tie/Mission_TIE95.txt

	// Such a file has null-terminated alphanumeric strings at 0x18 and
	// multiples of 64. Since the padding seems to be NULL everywhere on the
	// samples I've got, we'll just check the entire region for anything but
	// printable and NUL. If there's nothing else, it's a TIE file.

	// TBD

	bool only_TIE_bytes = true;

	for (size_t i = 24; i < 408 && only_TIE_bytes; ++i) {
		if (i >= contents_bytes.size()) { continue; }
		only_TIE_bytes &= (isprint(contents_bytes[i]) || contents_bytes[i] == 0);
	}

	// Also check the start of the strings: at least one of them should be
	// strictly printable.
	if (only_TIE_bytes) {
		bool letter_found = false;
		for (size_t i = 24; i < 408 && !letter_found; i += 64) {
			if (i >= contents_bytes.size()) { continue; }
			letter_found |= isprint(contents_bytes[i]);
		}
		only_TIE_bytes = letter_found;
	}

	if (only_TIE_bytes) {
		return "";
	}

	// We've run out of false positives to check against. Determine if it's
	// ZZT or Super ZZT.

	if (possible_header.magic == ZZT_HEADER_MAGIC) { return "zzt"; }
	return "szt";
}

bool is_brd(const std::vector<char> & contents_bytes) {
	// This is harder, but:

	// The first two bytes give the size of the board, which should be in the rough
	// range of the actual length of the contents. The contents chunk could be larger
	// if the web server appends junk to it, but it shouldn't have a size of say, zero.
	// Then comes the length of the board title, which should be shorter than 50 bytes.
	// That's about it; we can't say much about the RLE structure without dragging in
	// a whole interpreter...

	// Note that although the ZZT spec says that the board size should be signed (and
	// thus never exceed 32k), KevEdit will happily produce larger .BRD files.
	if (contents_bytes.size() < 53) { return false; }

	brd_header possible_header;
	memcpy(&possible_header, contents_bytes.data(), sizeof(brd_header));

	if (possible_header.board_size < 53 || (size_t)possible_header.board_size >
		contents_bytes.size()*2) { return false; }
	if (possible_header.title_length > 50 || possible_header.title_length < 0) {
		return false;
	}

	return true;
}

// TOIDO: Get surrounding data.

// Censor all but printables, and turn into a string.
std::string byte_str(const std::vector<char> & byte_sequence) {
	std::string out_str;
	out_str.reserve(byte_sequence.size());

	for (size_t i = 0; i < byte_sequence.size(); ++i) {
		if (isprint(byte_sequence[i])) {
			out_str += byte_sequence[i];
		} else {
			out_str += '_';
		}
	}
	return out_str;
}

// For text files: Do a keyword search and return a string describing the interest
// produced by this text, or the empty string if there's nothing of interest.

class false_match_data {
	public:
		std::string false_match;
		int offset;

		false_match_data(std::string fm_in, int off_in) {
			false_match = fm_in; offset = off_in;
		}
};

// Text

std::string data_interest_text(const std::string & body_text, bool exclude_long_words,
	const std::vector<std::string> & interesting_words) {

	// Don't match "BZZT". Also don't match ZZTop. The offsets (second part of the
	// tuple) gives how many letters precede "ZZT" in the false match.
	// Don't match *azzt (Mazzter, Jazztones) or *uzzt (Fuzztones, Fuzztrio) either.
	// Or *izzt (Drizzt, wizzt). Or other explosion/fizzle sounds like fzzt or ZZZT,
	// e.g. from http://geocities.com/TimesSquare/Stadium/4740/eb2epi2.html.

	std::vector<false_match_data> false_matches = {
		false_match_data("ZZTop", 0), false_match_data("ZZZT", 1),
		false_match_data("BZZT", 1), false_match_data("izzt", 1),
		false_match_data("azzt", 1), false_match_data("uzzt", 1),
		false_match_data("fzzt", 1), false_match_data("EMZXPYEI", 1),
		false_match_data("WA5ZZT", 3)
	};

	// Do a search for interesting words. This is the naive O(n^2) algorithm,
	// fix later if req'd.

	// Info about the characters surrounding the match.
	std::string context = "", matching_part = "";

	bool matched = false;
	for (std::string word: interesting_words) {
		if (matched) { continue; }

		size_t substr_pos = body_text.find(word);
		while (substr_pos != std::string::npos && !matched) {
			// Check for false positives.
			matched = true;
			for (false_match_data fm: false_matches) {
				if (!matched) { continue; }
				// If searching for the substring would put us out
				// of bounds, skippity.
				if ((int)substr_pos < fm.offset ||
					substr_pos + fm.false_match.size() - fm.offset >
						body_text.size()) {
					continue;
				}

				// Check against the specified false match.
				// TODO? Change the signature of this or make a shorthand funct?
				matching_part = std::string(
					body_text.begin() + substr_pos - fm.offset,
					body_text.begin() + substr_pos - fm.offset +
					fm.false_match.size());

				if (lower(matching_part) == lower(fm.false_match)) {
					matched = false;
				}
			}

			// Discard very long words. We make use of the side effect that
			// get_surrounding_data will return a full word containing the match
			// if one exists inside the window length.
			// Very long words are unlikely to be real matches, and more likely
			// to be Base64 stuff or similar.
			if (exclude_long_words && matched) {
				std::string long_window = get_surrounding_text(body_text,
					substr_pos, substr_pos + word.size(), 30, false);
				if (long_window.size() > 25) {
					matched = false;
				}
			}

			if (matched) {
				matching_part = std::string(body_text.begin() + substr_pos,
					body_text.begin() + substr_pos + word.size());
				context = get_surrounding_text(body_text,
					substr_pos, substr_pos + word.size(), 4, true);
				return "keyword(" + matching_part + ", " + context +")";
			}

			substr_pos = body_text.find(word, substr_pos+1);
		}
	}

	return "";
}

// TODO: Case insensitive MegaZeux, somehow

std::string data_interest_text(const std::string & body_text, bool exclude_long_words) {

	// NOTE: space has been set to be required for lowercase zzt and mzx due to
	// false positives. (See below re. "must be a word", which would be a better
	// constraint.)
	// TODO: Consider/investigate whether this would produce some false negatives.
	// It currently breaks the tests. (I'm thinking "zzters" etc would be a problem,
	// besides the obvious implementation deficiency of "It's zzt." not matching even
	// though "zzt" is a word here.)
	std::vector<std::string> interesting_words = { "ZZT", "MZX", "zzt ", "mzx ",
		"MegaZeux" };

	return data_interest_text(body_text, exclude_long_words, interesting_words);
}

// If searching binaries, demand a space at the end of the word, except for
// MegaZeux. BLUESKY: Also allow space at the beginning of the word (hard to
// do currently and this shouldn't be my priority at the moment).
std::string data_interest_binary_text(const std::string & body_text,
	bool exclude_long_words) {

	std::vector<std::string> interesting_words = { "ZZT ", "MZX ", "zzt ", "mzx ",
		"MegaZeux"/*, " ZZT", " MZX", " zzt", " mzx"*/ };

	return data_interest_text(body_text, exclude_long_words, interesting_words);
}

// HTML

#ifdef USE_LIBXML
// Recursively check all text elements (NOTE: Not links or other tag elements.
// This is part of the spec!)

// TODO: If this is slow, consider that other HTML parser that I don't remember
// the name of at the moment.
std::string data_interest_html(xmlNode * branch_root) {

	xmlNode * current_node = NULL;

	// If there is no node here, exit.
	if (branch_root == NULL) {
		return "";
	}

	for (current_node = branch_root; current_node != NULL;
		current_node = current_node->next) {

		if(current_node->type == XML_TEXT_NODE) {
			std::string text_interest = data_interest_text(
				(char *)current_node->content, true);
			if (text_interest != "") {
				return text_interest;
			}
		}

		std::string subtree_interest = data_interest_html(current_node->children);
		if (subtree_interest != "") {
			return subtree_interest;
		}
	}

	return "";
}

// libxml2 takes bytes as input.
std::string data_interest_html(const std::vector<char> & contents_bytes) {

	// We don't need the encoding. Since we aren't examining the
	// links, we don't need a base URL either.
	htmlDocPtr doc = htmlReadMemory(
		contents_bytes.data(), contents_bytes.size(),
		NULL, get_magic_encoding(contents_bytes).data(),
		HTML_PARSE_RECOVER | HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR |
		HTML_PARSE_NOWARNING | HTML_PARSE_NONET);

	if (doc == NULL) {
		// TODO: Somehow signal error. C++ exceptions are generally too
		// slow and an adversarial server could make us produce an arbitrary
		// amount of them...
		return ""; // Error parsing document.
	}

	std::string interest_type = data_interest_html(xmlDocGetRootElement(doc));

	xmlFreeDoc(doc);	// Free the document we read.
	xmlCleanupParser();	// And free globals. (Should this be called once per,
						// or just once at the end of the program?)

	return interest_type;
}
#endif

// Lightly adapted from libarchive's simple archive reading example.
// TODO: Handle internal paths more gracefully (yuck). Also TODO: Handle corrupted
// ZIPs inside OK ZIPs without throwing an exception and bailing out.

// Something like...

const int PRI_KEYWORD = 1, PRI_EXTENSION = 2,
			PRI_CONTENT = 3;

class interest_data {
	public:
		bool archive;
		int priority;				// higher is more important
		std::string internal_path;	// treats archives as directories
		std::string interest_type;

		// Perhaps add this:
		//std::string error;
		// which denotes a non-fatal error that might still produce false
		// negatives (e.g. unsupported compression method, corrupted file).

		// HACK. TODO: Do an overload instead
		std::string str() const {
			std::string interest_string;

			if (interest_type == "") {
				return interest_string;
			}

			if (internal_path == "") {
				return interest_type;
			}

			if (archive) {
				return "archive[" + internal_path + "]:" + interest_type;
			} else {
				return internal_path + ":" + interest_type;
			}
		}

		bool interesting() const { return interest_type != ""; }

		interest_data() {}

		interest_data(int priority_in, const std::string & interest_type_in) {
			archive = false;
			priority = priority_in;
			interest_type = interest_type_in;
		}
};

std::vector<interest_data> data_interest_type(const std::string & file_path,
	std::string mime_type, const std::vector<char> & contents_bytes,
	int recursion_level);

std::string coarse_libarchive_error(int ret_val) {
	switch(ret_val) {
		case ARCHIVE_RETRY: return "ARCHIVE_RETRY";
		case ARCHIVE_WARN: return "ARCHIVE_WARN";
		case ARCHIVE_FAILED: return "ARCHIVE_FAILED";
		case ARCHIVE_FATAL: return "ARCHIVE_FATAL";
		default: return "Unknown libarchive error";
	}
}

// Check if an archive contains interesting files. The recursion level parameter
// stops quines and ZIP bombs from causing infinite loops.

std::vector<interest_data> data_interest_archive(const std::string & file_path,
	const std::vector<char> & contents_bytes, int recursion_level) {

	std::vector<interest_data> interesting_files;

	if (recursion_level <= 0) { return interesting_files; }

	archive * cur_archive;
	archive_entry * entry;

	int ret_val;

	cur_archive = archive_read_new();
	archive_read_support_filter_all(cur_archive);
	archive_read_support_format_all(cur_archive);
	ret_val = archive_read_open_memory(cur_archive, contents_bytes.data(),
		contents_bytes.size());

	if (ret_val != ARCHIVE_OK) {
		std::string err = archive_error_string(cur_archive);
		archive_read_free(cur_archive);
		throw(std::runtime_error("Error unpacking archive " + file_path + ": " +
			err));
	}

	std::runtime_error inherited_exception("placeholder");
	bool got_exception = false;
	ret_val = ARCHIVE_OK;

	std::vector<interest_data> interesting_in_file;

	while (ret_val == ARCHIVE_OK || ret_val == ARCHIVE_WARN) {

		ret_val = archive_read_next_header(cur_archive, &entry);
		if (ret_val != ARCHIVE_OK && ret_val != ARCHIVE_WARN) {
			continue;
		}

		// Work around a libarchive limitation where non-ASCII letters can
		// cause archive_entry_pathname to return NULL.
		// I can't be arsed to fix it in source.
		// https://github.com/libarchive/libarchive/issues/1572
		std::string inner_pathname = "<UNKNOWN FOREIGN>";
		const char * pathname_ptr = archive_entry_pathname(entry);
		if (pathname_ptr != NULL) {
			inner_pathname = pathname_ptr;
		}

		size_t inner_file_size = archive_entry_size(entry);

		// Avoid ZIP bombs and other very large files. (32M max)
		if (inner_file_size > (1<<25)) { continue; }

		std::vector<char> unpacked_bytes(inner_file_size);
		int bytes_read = archive_read_data(cur_archive, unpacked_bytes.data(),
			unpacked_bytes.size());

		// Check if the compression method is supported. If not, clear the data
		// array so we can still check for extensions and the likes.
		if (bytes_read < 0) {
			std::cerr << "xERROR: " << file_path << "\t" << archive_error_string(cur_archive) << std::endl;
			unpacked_bytes.resize(0);
			bytes_read = 0;
		}

		try {
			interesting_in_file = data_interest_type( file_path + "/" +
				inner_pathname, "", unpacked_bytes, recursion_level-1);
		} catch (const std::runtime_error & e) {
			// If we got an exception, the rule is: if we find something else that's
			// interesting, forget it happened; but if we don't find anything, then
			// propagate it up, because the corrupt archive might have been
			// interesting.
			inherited_exception = e;
			got_exception = true;
		}

		// If we didn't find anything, but the file has the right extension, report
		// interest from the extension itself, as we're a bit more lenient here than
		// with uncompressed files. (XXX: Why? I think it's because the file may
		// be corrupted.)
		if (interesting_in_file.empty()) {
			std::string extension = lower(file_path.begin() + file_path.size() - 4,
				file_path.end());
			if (str_contains(extension, { ".zzt", ".brd", ".mzx", ".mzb", ".szt",
				".zzm", ".zzl", ".zz3"})) {
				interest_data inner_interest_data(PRI_EXTENSION,
					"extension");
				interesting_in_file.push_back(inner_interest_data);
			}
		}

		// Append the path to the archive to the interest data so that whatever
		// is calling us will know where the interesting files are stored. Also
		// set the archive bit and push the interest data to interesting_files.

		for (interest_data id: interesting_in_file) {
			id.archive = true;

			if (id.internal_path == "") {
				id.internal_path = inner_pathname;
			} else {
				id.internal_path = inner_pathname + "/" + id.internal_path;
			}

			interesting_files.push_back(id);
		}
	}

	if (ret_val != ARCHIVE_OK && ret_val != ARCHIVE_EOF) {
		std::string what;

		if (archive_error_string(cur_archive)) {
			what = "Error reading from archive " + file_path + ": " +
				archive_error_string(cur_archive);
		} else {
			what = "Error reading from archive " + file_path + ": " +
				coarse_libarchive_error(ret_val);
		}
		archive_read_free(cur_archive);
		throw (std::runtime_error(what));
	}

	ret_val = archive_read_free(cur_archive);
	if (ret_val != ARCHIVE_OK) {
		throw(std::runtime_error("Error freeing archive " + file_path + ": " +
			archive_error_string(cur_archive)));
	}

	if (interesting_files.empty() && got_exception) {
		throw(inherited_exception);
	}

	return interesting_files;
}

// recursion_level is used internally (see data_interest_archive).

// TODO: Return three values: the path to the file that's interesting,
// the way it's interesting, and any potential errors. Perhaps also a bool
// denoting whether it *is* interesting.

std::vector<interest_data> data_interest_type(const std::string & file_path,
	std::string mime_type, const std::vector<char> & contents_bytes,
	int recursion_level) {

	std::string magic_mime_type = get_magic_mimetype(contents_bytes);

	if (mime_type == "") {
		mime_type = magic_mime_type;
	}

	std::string extension = lower(file_path.begin() + file_path.size() - 4,
		file_path.end());

	// If it has the proper extension for files we can't verify the contents
	// of, that's interesting.
	if (str_contains(extension, { ".mzx", ".mzb", ".zz3", ".zzl", ".zzm" })) {
		return {interest_data(PRI_EXTENSION, "extension")};
	}

	std::string szt_zzt;

	// ZZT save files are denoted .SAV and are really just ZZT files. Check
	// for them. (BLUESKY: also check MegaZeux saves, which share the extension).
	// TODO: move a bunch of the string to interest data stuff to the internal
	// functions.
	if (str_contains(extension, { ".szt", ".zzt", ".sav"})) {
		szt_zzt = zzt_szt_check(contents_bytes, false);
	} else {
		szt_zzt = zzt_szt_check(contents_bytes, true);
	}

	if (szt_zzt != "") { return {interest_data(PRI_CONTENT, szt_zzt)}; }

	// SZT and ZZT are sufficiently rare extensions that we flag them even if
	// the above check doesn't trigger. This is useful for corrupted compressed
	// data or data compressed using an unsupported method such as Implode.
	if (str_contains(extension, std::vector<std::string>({ ".szt", ".zzt"}))) {
		return {interest_data(PRI_EXTENSION, "extension")};
	}

	// BRD file, always check. This is sufficiently prone to false positives
	// that we only check with the correct extension.
	if (str_contains(extension, ".brd")) {
		if (is_brd(contents_bytes)) {
			return {interest_data(PRI_CONTENT, "brd")};
		}
	}

	// Don't check images, audio files or video files.
	if (str_contains(mime_type, {"audio/", "video/", "image/"})) {
		return {};
	}
	// Or Shockwave Flash files
	if (str_contains(mime_type, "application/x-shockwave-flash")) {
		return {};
	}

#ifdef USE_LIBXML
	// TODO: XML?
	if (str_contains(mime_type, "html")) {
		std::string possible_interest = data_interest_html(contents_bytes);
		if (possible_interest != "") {
			return { interest_data(PRI_KEYWORD, possible_interest)}; }
	}
#endif

	// Stringify the contents so we can search it with data_interest_text.
	// TODO: make the latter work directly on the vectors.
	std::string contents_text = to_str(contents_bytes);

	if (str_contains(mime_type, "text/")) {
		// Text stuff goes here. Since all our search terms are pure ASCII,
		// there's no need for Unicode shenanigans (yet).
		std::string possible_interest = data_interest_text(contents_text, true);
		if (possible_interest != "") {
			return {interest_data(PRI_KEYWORD, possible_interest)}; }
	}

	// I think I've got all the formats libarchive supports -- except mtree and zip.uu.
	std::vector<std::string> archive_mimetypes = {
		"application/zip", "application/gzip", "application/x-tar", "application/x-arj",
		"application/x-lzh-compressed", "application/x-rar", "application/x-cpio",
		"application/x-iso9660-image", "application/vnd.ms-cab-compressed",
		"application/x-archive", "application/x-bzip2", "application/x-lz4",
		"application/x-lzop", "application/x-7z-compressed", "application/x-xz"
	};

	std::vector<std::string> archive_extensions = {".zip", ".gz", ".tar", ".arj",
		".lzh", ".rar", ".cpio", ".iso", ".cab", ".ar", ".bz2", ".lz4",
		".lzo", ".lzop", ".lzs", ".lzw", ".7z", ".xz"
	};

	// These can be archives, but also have other uses: don't count corrupt results
	// against it. This list is mainly dos executables (self-extracting archives).
	std::vector<std::string> possible_archive_mimetypes = {
		"application/x-dosexec", "application/octet-stream"
	};

	std::vector<std::string> possible_archive_extensions = { ".exe" };

	// If it's an archive, test separately.
	bool archive = str_contains(mime_type, archive_mimetypes) ||
		str_contains(magic_mime_type, archive_mimetypes) ||
		str_contains(extension, archive_extensions);
	bool maybe_archive = false;
	if (!archive) {
		maybe_archive = str_contains(extension, possible_archive_extensions);
	}

	if (archive || maybe_archive) {
		try {
			std::vector<interest_data> interesting_in_file =
				data_interest_archive(file_path, contents_bytes,
					recursion_level);
			if (!interesting_in_file.empty()) {
				return interesting_in_file;
			}
		} catch (const std::runtime_error & e) {
			// Don't signal corrupt archive file for something that's primarily not
			// an archive.
			if (!maybe_archive) {
				std::cerr << "WARNING: Corrupt archive file " << file_path << ", " << e.what() << std::endl;
			}
		}
	}

	// If all else fails, look for keywords in the binary stream.
	// TODO: Don't call this if we checked a text or html file.
	std::string possible_interest = data_interest_binary_text(contents_text, true);
	if (possible_interest != "") {
		return { interest_data(PRI_KEYWORD, possible_interest)}; }

	return {};

}

std::vector<std::string> data_interest_type(const std::string & file_path,
	const std::string & mime_type, const std::vector<char> & contents_bytes) {

	std::vector<std::string> results;
	for (const interest_data & id: data_interest_type(
		file_path, mime_type, contents_bytes, 3)) {
		results.push_back(id.str());
	}

	return results;
}

std::string highest_priority_interest_type(const std::string & file_path,
	const std::string & mime_type, const std::vector<char> & contents_bytes) {

	std::vector<interest_data> interesting_files =
		data_interest_type(file_path, mime_type, contents_bytes, 3);

	int record_priority = INT_MIN;
	std::string recordholder = "";

	for (const interest_data & id: interesting_files) {
		if (!id.interesting() || id.priority < record_priority) {
			continue;
		}
		record_priority = id.priority;
		recordholder = id.str();
	}

	return recordholder;
}

// --- //

bool is_data_interesting(std::string filename, std::string mime_type,
	const std::vector<char> & contents_bytes) {

	return !data_interest_type(filename, mime_type, contents_bytes).empty();
}

// TODO: Add more tests from Python

bool TEST_interesting() {
	std::vector<std::vector<char> > must_be_interesting = {
		vecs("I like ZZT!"),
		vecs("I like ZZTop and ZZT!"),
		vecs("ZZT is my favorite game"),
		vecs("ZZTers and MZXers welcome"),
		vecs("MZXers and zzters welcome"),
		vecs("I heard there's a game called zzt or something"),
		// TODO: Make this work (i.e. ZZT must be a word, but doesn't need
		// to be flanked by spaces)
		//vecs("I heard there's a game called zzt."),
		byte_seq("ladeda\x00" " ZZT \x00", 13)
	};

	std::vector<std::vector<char> > must_be_uninteresting = {
		vecs("I like ZZTop!"),
		vecs("Lyrics ZZTop Lyrics"),
		vecs("I LIKE ZZTOP!"),
		vecs("i like zztop."),
		vecs("It went BZZT!"),
		vecs("DRIZZT IS THE COOLEST DROW"),
		vecs("Jazztones"),
		vecs("Fuzztrio"),
		vecs("BUZZTHRILL"),
		vecs("BZZZT"),
		vecs("The explosion fizzled with a fzzt"),
		// High-entropy strings that sometimes appear in Blogspot HTML files.
		vecs("window['__wavt'] = 'AOuZoY5I4fZZTlyPDxtXnUDukaEWGpHwrQ:1632960341455';_Widget"),
		vecs("<br />RsJ4NMZXYyyEdGrVjPlaolMdrCDGfsmzNeLU8kcFmr24xU6Y4AZ4nVJ87gaR5pINT/RIV0zm"),
		// Very long string with match at the end, and at the beginning
		vecs("__________________________________________________mzx"),
		vecs("zzt_______________________________________________"),
		// Serial numbers found in the Geocities archive
		vecs("/FMD027877 pw/EMZXPYEI"),
		vecs("WA5ZZT") //Ham radio code
	};

	bool all_OK = true;

	for (std::vector<char> test_str: must_be_interesting) {
		if (!is_data_interesting("test.txt", "text/plain", test_str)) {
			std::cerr << "Test fail: " << std::string(test_str.begin(), test_str.end())
				<< " not interesting.\n";
			all_OK = false;
		}
	}

	for (std::vector<char> test_str: must_be_uninteresting) {
		// Test both plaintext and binary data. Nothing that's uninteresting for
		// text should be interesting for binary data!

		if (is_data_interesting("test.txt", "text/plain", test_str)) {
			std::cerr << "Test fail: " << std::string(test_str.begin(), test_str.end())
				<< " interesting.\n";
			all_OK = false;
		}

		if (is_data_interesting("test.dat", "text/html", test_str)) {
			std::cerr << "Test fail: " << std::string(test_str.begin(), test_str.end())
				<< " interesting.\n";
			all_OK = false;
		}
	}

	// HTML, etc goes here.

	return all_OK;
}
