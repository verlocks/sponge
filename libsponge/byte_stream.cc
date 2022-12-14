#include "byte_stream.hh"

#include <stdexcept>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) :  _buffer(), _capacity(capacity) {
    if (capacity == 0) {
        throw runtime_error("invalid capacity:" + to_string(capacity));
    }
}

size_t ByteStream::write(const string &data) {
    size_t ret = min(remaining_capacity(), data.size());
    _write_count += ret;
    _buffer += data.substr(0, ret);
    return ret;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    auto ret = _buffer.substr(0, len);
    return ret;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    _read_count += min(len, _buffer.size());
    _buffer.erase(0, len);
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    auto ret = peek_output(len);
    pop_output(len);
    return ret;
}

void ByteStream::end_input() { _eof = true; }

bool ByteStream::input_ended() const { return _eof; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const { return _buffer.size() == 0 && input_ended(); }

size_t ByteStream::bytes_written() const { return _write_count; }

size_t ByteStream::bytes_read() const { return _read_count; }

size_t ByteStream::remaining_capacity() const { return (_capacity > _buffer.size()) ? _capacity - _buffer.size() : 0; }
