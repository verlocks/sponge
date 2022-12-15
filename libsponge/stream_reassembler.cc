#include "stream_reassembler.hh"

#include <iostream>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void StreamReassembler::_next_buffer_head() {
    _buffer_head = (_buffer_head == _capacity - 1) ? 0 : _buffer_head + 1;
}

void StreamReassembler::_push_buffer() {
    while (_mask[_buffer_head] && _output.remaining_capacity() > 0) {
        _output.write(string(1, _buffer[_buffer_head]));
        _mask[_buffer_head] = false;
        _next_buffer_head();
        _curr_index++;
        _num_in_buffer--;
    }
}

void StreamReassembler::_write_into_buffer(const string &data, const size_t index, const bool eof) {
    size_t i = 0;
    while (i < data.size() && _curr_index <= index + i && index + i < _curr_index + _capacity) {
        size_t _i = (index + i - _curr_index + _buffer_head) % _capacity;
        if (!_mask[_i]) {
            _mask[_i] = true;
            _buffer[_i] = data[i];
            _num_in_buffer++;
        }
        i++;
    }
    if (index + data.size() <= _curr_index + _capacity)
        _eof = _eof || eof;
    if (_num_in_buffer == 0 && _eof)
        _output.end_input();
}

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity), 
                                                              _buffer(capacity), _mask(capacity, false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    _push_buffer();

    while (index <= _curr_index && _curr_index < index + data.size() && _output.remaining_capacity() > 0) {
        _output.write(string(1, data[_curr_index - index]));
        _curr_index++;
        if (_mask[_buffer_head]) {
            _mask[_buffer_head] = false;
            _num_in_buffer--;
        }
        _next_buffer_head();
    }

    _push_buffer();
    _write_into_buffer(data, index, eof);
}

size_t StreamReassembler::unassembled_bytes() const { return _num_in_buffer; }

bool StreamReassembler::empty() const { return _num_in_buffer == 0; }
