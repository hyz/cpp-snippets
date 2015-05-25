#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <fstream>

std::string get_http_data(const std::string& server, const std::string& file)
{
	try
	{
		boost::asio::ip::tcp::iostream s(server, "http");
		s.expires_from_now(boost::posix_time::seconds(60));

		if (!s){ throw "Unable to connect: " + s.error().message(); }

		// ask for the file
		s << "GET " << file << " HTTP/1.0\r\n";
		s << "Host: " << server << "\r\n";
		s << "Accept: */*\r\n";
		s << "Connection: close\r\n\r\n";

		// Check that response is OK.
		std::string http_version;
		s >> http_version;
		unsigned int status_code;
		s >> status_code;
		std::string status_message;
		std::getline(s, status_message);
		if (!s && http_version.substr(0, 5) != "HTTP/"){ throw "Invalid response\n"; }
		if (status_code != 200){ throw "Response returned with status code " + status_code; }

		// Process the response headers, which are terminated by a blank line.
		std::string header;
		while (std::getline(s, header) && header != "\r"){}

		// Write the remaining data to output.
		std::stringstream ss;
		ss << s.rdbuf();
		return ss.str();
	}
	catch(std::exception& e)
	{
		return e.what();
	}
}

int main() 
{
//http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2011/n3242.pdf
  std::string result = get_http_data("www.open-std.org", "/jtc1/sc22/wg21/docs/papers/2011/n3242.pdf");

  std::ofstream of("cpp11_draft_n3242.pdf", std::ios::binary);
  of << result;
}

