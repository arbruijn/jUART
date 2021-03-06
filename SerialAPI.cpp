/****************************************************************/
// By Bill Hsu
// Shanghai University
// My Blog:        http://BillHsu.me
// jUART Project:  http://github.com/billhsu/jUART
// Created on:     2013-01-09
/****************************************************************/

// Thanks to minicom.cpp

#include "SerialAPI.h"
#include <boost/thread.hpp> 
#include <boost/exception/all.hpp>
#include <boost/exception/get_error_info.hpp>

SerialAPI::SerialAPI(const FB::BrowserHostPtr& host, const int securityZone) : JSAPIAuto(securityZone, "<JSAPI-Auto Secure Javascript Object>"),
	m_host(host),io(), serial(io)
{
	FB::scoped_zonelock _l(this, FB::SecurityScope_Protected);
    registerMethod("open",  make_method(this, &SerialAPI::open));
    registerMethod("set_option",  make_method(this, &SerialAPI::set_option));
    registerMethod("send",  make_method(this, &SerialAPI::send));
    registerMethod("sendtest",  make_method(this, &SerialAPI::sendtest));
    registerMethod("is_open",  make_method(this, &SerialAPI::is_open));
    registerMethod("recv_callback",  make_method(this, &SerialAPI::recv_callback));
    registerMethod("err_callback",  make_method(this, &SerialAPI::err_callback));
    registerMethod("close",  make_method(this, &SerialAPI::close));
    registerProperty("ports",  make_property(this, &SerialAPI::get_ports));
}

SerialAPI::~SerialAPI(void)
{
    m_thread.interrupt();
    close();
}

bool SerialAPI::open(std::string _device)
{
    try
    {
        serial.open(_device);
    }
    catch (boost::exception& e)
    {
    	if(m_err_callback)
            m_recv_callback->InvokeAsync("", FB::variant_list_of
            (boost::get_error_info<boost::errinfo_file_open_mode>(e)));
    }
    if(serial.is_open())device = _device;
    return serial.is_open();

}

bool SerialAPI::set_option(unsigned int baud, unsigned int parity,
    unsigned int csize, unsigned int flow, unsigned int stop)
{
    if(!serial.is_open())   return false;

    boost::asio::serial_port_base::baud_rate opt_baud(baud);
    boost::asio::serial_port_base::parity opt_parity(
        static_cast<boost::asio::serial_port_base::parity::type>(parity));
    boost::asio::serial_port_base::character_size opt_csize(csize);
    boost::asio::serial_port_base::flow_control opt_flow(
        static_cast<boost::asio::serial_port_base::flow_control::type>(flow));
    boost::asio::serial_port_base::stop_bits opt_stop(
        static_cast<boost::asio::serial_port_base::stop_bits::type>(stop));

    serial.set_option(opt_baud);
    serial.set_option(opt_parity);
    serial.set_option(opt_csize);
    serial.set_option(opt_flow);
    serial.set_option(opt_stop);
    recv_start();

    m_thread = boost::thread(boost::bind(&boost::asio::io_service::run, &io));

    return true;
}

void SerialAPI::recv_callback(const FB::JSObjectPtr& callback)
{
    m_recv_callback = callback;
}

void SerialAPI::err_callback(const FB::JSObjectPtr& callback)
{
    m_err_callback = callback;
}

void SerialAPI::recv_start(void) 
{ // Start an asynchronous read and call read_complete when it completes or fails 
    serial.async_read_some(boost::asio::buffer(recv_msg, max_buffer_length), 
        boost::bind(&SerialAPI::recv_complete, 
        this, 
        boost::asio::placeholders::error, 
        boost::asio::placeholders::bytes_transferred)); 
}

void SerialAPI::recv_complete(const boost::system::error_code& error, size_t bytes_transferred) 
{ // the asynchronous read operation has now completed or failed and returned an error 
    if (!error) 
    { // read completed, so process the data 
        //cout.write(recv_msg, bytes_transferred); // echo to standard output
        std::vector<char> valVec(recv_msg, recv_msg+bytes_transferred);
        FB::VariantList vars = FB::make_variant_list(valVec);

        if(m_recv_callback)
            m_recv_callback->InvokeAsync("", FB::variant_list_of
            (vars)
            (bytes_transferred));
        recv_start(); // start waiting for another asynchronous read again 
    } 
    else 
        do_close(error); 
} 

void SerialAPI::do_send(const char msg) 
{
    bool write_in_progress = !send_msg.empty(); // is there anything currently being written? 
    send_msg.push_back(msg); // store in write buffer 
    if (!write_in_progress) // if nothing is currently being written, then start 
        send_start(); 
} 

void SerialAPI::send_start(void)
{ // Start an asynchronous write and call write_complete when it completes or fails 
    boost::asio::async_write(serial, 
        boost::asio::buffer(&send_msg.front(), 1), 
        boost::bind(&SerialAPI::send_complete, 
        this, 
        boost::asio::placeholders::error)); 
} 

void SerialAPI::send_complete(const boost::system::error_code& error) 
{ // the asynchronous read operation has now completed or failed and returned an error 
    if (!error) 
    { // write completed, so send next write data 
        send_msg.pop_front(); // remove the completed data 
        if (!send_msg.empty()) // if there is anything left to be written 
            send_start(); // then start sending the next item in the buffer 
    } 
    else 
        do_close(error); 
} 

void SerialAPI::do_close(const boost::system::error_code& error) 
{
    if (error == boost::asio::error::operation_aborted) // if this call is the result of a timer cancel() 
        return; // ignore it because the connection canceled the timer 
    
    serial.close(); 
}

FB::VariantList SerialAPI::get_ports(void)
{
    FB::VariantList valVec;

#ifdef WIN32
	HKEY hKey;
    if (::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return -1;

    for (DWORD dwIndex = 0;; dwIndex++) {
        TCHAR valueName[128];
        BYTE valueData[128];
        DWORD cbName = sizeof(valueName) / sizeof(valueName[0]);
        DWORD cbData = sizeof(valueData) / sizeof(valueData[0]);
        LONG lRet;
        if ((lRet = ::RegEnumValue(hKey, dwIndex, valueName, &cbName, NULL,
            NULL, valueData, &cbData))) {
            if (lRet == ERROR_NO_MORE_ITEMS)
                break;
            // report error...
            break;
        }
        std::vector<std::wstring> pair(2);
        pair[0] = std::wstring((wchar_t *)szValueData);
        pair[1] = std::wstring(szValueName);
        vec.push_back(FB::make_variant_list(pair));
    }
    ::RegCloseKey(hKey);
#endif

    return valVec;
}
