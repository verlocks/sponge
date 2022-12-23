#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _curr_rto(retx_timeout)
    , _curr_time(retx_timeout)
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    uint16_t bytes_sent = 0;

    while (!_fin_sent && bytes_sent < _window_size) {
        TCPSegment segment;
        TCPHeader &header = segment.header();
        Buffer &payload = segment.payload();
        size_t len;

        header.seqno = wrap(_next_seqno, _isn);
        if (_next_seqno == 0) {
            header.syn = true;
            bytes_sent += 1;
        } else if (_stream.buffer_empty() && !_stream.input_ended())
            break;

        len = _window_size - bytes_sent;
        payload = Buffer(_stream.read(min(TCPConfig::MAX_PAYLOAD_SIZE, len)));
        bytes_sent += payload.size();

        if (bytes_sent < _window_size && _stream.buffer_empty() && _stream.input_ended()) {
            header.fin = true;
            bytes_sent += 1;
            _fin_sent = true;
        }

        _pend_list[_next_seqno] = segment;
        _segments_out.push(segment);
        _next_seqno += segment.length_in_sequence_space();
        _bytes_in_flight += segment.length_in_sequence_space();
        _time_stop = false;

        if (_stream.buffer_empty())
            break;
    }
    _window_size -= bytes_sent;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    const uint64_t ackno_abs = unwrap(ackno, _isn, _next_seqno);
    if (ackno_abs > _next_seqno)
        return;

    _recv_window_size = window_size;
    const uint16_t new_window_size = window_size == 0 ? 1 : window_size;
    _bytes_in_flight = _next_seqno > ackno_abs ? _next_seqno - ackno_abs : 0;
    if (_bytes_in_flight == 0)
        _time_stop = true;
    if (ackno_abs > _prev_ackno_abs) {
        _prev_ackno_abs = ackno_abs;
        _curr_rto = _initial_retransmission_timeout;
        _curr_time = _curr_rto;
        _consecutive_retrans_time = 0;
    }

    auto it = _pend_list.begin();
    for (; it != _pend_list.end(); ++it) {
        const uint64_t curr_seqno = it->first;
        const TCPSegment segment = it->second;

        if (curr_seqno + segment.length_in_sequence_space() > ackno_abs)
            break;
    }
    _pend_list.erase(_pend_list.begin(), it);

    _window_size = ackno_abs + new_window_size > _next_seqno ? ackno_abs + new_window_size - _next_seqno : 0;
    if (ackno_abs + new_window_size > _next_seqno)
        fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (_time_stop)
        return;

    _curr_time = _curr_time > ms_since_last_tick ? _curr_time - ms_since_last_tick : 0;
    if (_curr_time == 0) {
        const TCPSegment segment = _pend_list.begin()->second;
        _segments_out.push(segment);

        if (_recv_window_size != 0) {
            if (_consecutive_retrans_time == 0 || _last_retrans != _pend_list.begin()->first) {
                _last_retrans = _pend_list.begin()->first;
                _consecutive_retrans_time = 1;
            } else
                _consecutive_retrans_time += 1;

            _curr_rto *= 2;
        }

        _curr_time = _curr_rto;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retrans_time; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    _segments_out.push(segment);
}
