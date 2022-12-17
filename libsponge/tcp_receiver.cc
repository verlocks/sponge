#include "tcp_receiver.hh"

#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader header = seg.header();
    std::string data = seg.payload().copy();

    if (header.syn && !_ackno.has_value()) {
        _isn = header.seqno;
        _ackno = _isn + 1;
    }

    if (!_ackno.has_value())
        return;

    uint64_t checkpoint = stream_out().bytes_written() + 1;
    uint64_t index = header.syn ? unwrap(header.seqno + 1, _isn, checkpoint) : unwrap(header.seqno, _isn, checkpoint);

    _reassembler.push_substring(data, index - 1, header.fin);
    checkpoint = stream_out().bytes_written() + 1;
    if (stream_out().input_ended())
        checkpoint++;
    _ackno = wrap(checkpoint, _isn);
}

optional<WrappingInt32> TCPReceiver::ackno() const { return _ackno; }

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
