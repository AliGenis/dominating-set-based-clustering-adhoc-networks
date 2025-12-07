import inet.common.packet.chunk.Chunk;

namespace src;

class DsHelloPacket extends inet::FieldsChunk
{
    @packetData;
    string senderAddress;
    int state;
    string neighbors[];
}