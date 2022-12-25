#include "tcp_connection.hh"

#include <iostream>
#include <limits>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

// segments_out should be guaranteed to have segement when called.
void TCPConnection::_wrap_next_segment_and_send() {
    TCPSegment &segment = _sender.segments_out().front();
    TCPHeader &header = segment.header();
    header.win = _receiver.window_size() > numeric_limits<uint16_t>::max() ? numeric_limits<uint16_t>::max()
                                                                           : _receiver.window_size();
    if (_receiver.ackno().has_value()) {
        header.ack = true;
        header.ackno = _receiver.ackno().value();
    }
    _sender.segments_out().pop();
    _segments_out.push(segment);
}

void TCPConnection::_abort_connection() {
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _active = false;
}

void TCPConnection::_send_rst_segment() {
    TCPSegment segment;
    TCPHeader &header = segment.header();
    header.rst = true;
    _segments_out.push(segment);

    _abort_connection();
}

bool TCPConnection::_connection_finished() {
    if (_linger_after_streams_finish)
        return _receiver.stream_out().input_ended() && _sender.stream_in().input_ended() &&
               _sender.bytes_in_flight() == 0 && _time_since_last_segment_received >= 10 * _cfg.rt_timeout;
    else
        return _receiver.stream_out().input_ended() && _sender.stream_in().input_ended() &&
               _sender.bytes_in_flight() == 0;
}

void TCPConnection::_check_connection() {
    if (_connection_finished())
        _active = false;
}

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active)
        return;

    const TCPHeader &header = seg.header();
    std::optional<WrappingInt32> new_ackno;

    _time_since_last_segment_received = 0;

    if (header.rst) {
        _abort_connection();
        return;
    }

    if (!_receiver.ackno().has_value() && !header.syn)
        return;

    _receiver.segment_received(seg);

    if (_receiver.stream_out().input_ended() && !_sender.stream_in().input_ended())
        _linger_after_streams_finish = false;

    if (header.ack)
        _sender.ack_received(header.ackno, header.win);

    new_ackno = _receiver.ackno();
    if (seg.length_in_sequence_space() > 0) {
        _sender.fill_window();
        if (_sender.segments_out().empty())
            _sender.send_empty_segment();
        while (!_sender.segments_out().empty())
            _wrap_next_segment_and_send();
    } else if (new_ackno.has_value() && seg.length_in_sequence_space() == 0 && header.seqno == new_ackno.value() - 1) {
        _sender.send_empty_segment();
        while (!_sender.segments_out().empty())
            _wrap_next_segment_and_send();
    }

    _check_connection();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t ret;

    ret = _sender.stream_in().write(data);
    _sender.fill_window();
    while (!_sender.segments_out().empty())
        _wrap_next_segment_and_send();

    _check_connection();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        _send_rst_segment();
        return;
    }

    while (!_sender.segments_out().empty())
        _wrap_next_segment_and_send();

    _time_since_last_segment_received += ms_since_last_tick;
    _check_connection();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    while (!_sender.segments_out().empty())
        _wrap_next_segment_and_send();
    _check_connection();
}

void TCPConnection::connect() {
    _sender.fill_window();
    while (!_sender.segments_out().empty())
        _wrap_next_segment_and_send();
    _check_connection();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _send_rst_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
