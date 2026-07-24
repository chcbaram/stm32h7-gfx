#!/usr/bin/env python3
"""stm32h7-lvgl cmd 프로토콜 링크

펌웨어의 src/hw/driver/cmd.c 와 동일한 패킷 형식을 사용한다.

  STX0(0x02) STX1(0xFD) TYPE CMD_L CMD_H ERR_L ERR_H LEN_L LEN_H [DATA] CHECKSUM

주의: 보율은 115200 을 쓰면 안 된다.
펌웨어가 115200 으로 열린 포트를 CLI 로 판단해서 CLI 가 USB 채널을 잡는다.
그 외 보율이면 CLI 는 SWD UART 로 물러나고 이 채널을 cmd 가 단독으로 쓴다.
"""

import serial

STX0 = 0x02
STX1 = 0xFD

PKT_TYPE_CMD = 0x00
PKT_TYPE_RESP = 0x01

# 펌웨어와 동일한 보율 규칙
BAUD_CMD = 921600
BAUD_CLI = 115200

# src/common/err_code.h
ERR_STR = {
    0x0000: "OK",
    0x0020: "ERR_CMD_MAX_LENGTH",
    0x0021: "ERR_CMD_CHECKSUM",
    0x0022: "ERR_CMD_RX_TIMEOUT",
    0x0023: "ERR_CMD_NO_CMD",
    0x0040: "ERR_FILE_OPEN",
    0x0041: "ERR_FILE_WRITE",
    0x0042: "ERR_FILE_READ",
    0x0043: "ERR_FILE_NOT_BEGIN",
    0x0044: "ERR_FILE_SIZE",
    0x0045: "ERR_FILE_CRC",
    0x0046: "ERR_FILE_NAME",
    0x0047: "ERR_FILE_DEL",
}


def err_str(code):
    return ERR_STR.get(code, f"0x{code:04X}")


class CmdError(Exception):
    def __init__(self, cmd, err_code):
        super().__init__(f"cmd 0x{cmd:04X} failed : {err_str(err_code)}")
        self.cmd = cmd
        self.err_code = err_code


def _make_crc_table():
    """src/common/core/util.c 의 util_crc_table 과 동일 (poly 0x8005)"""
    table = []
    for i in range(256):
        crc = i << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        table.append(crc)
    return table


_CRC_TABLE = _make_crc_table()


def crc16(data, crc=0):
    """펌웨어의 utilUpdateCrc() 와 동일한 CRC16"""
    for b in data:
        crc = ((crc << 8) ^ _CRC_TABLE[((crc >> 8) ^ b) & 0xFF]) & 0xFFFF
    return crc


class CmdLink:
    def __init__(self, port, baud=BAUD_CMD, timeout=2.0):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.timeout = timeout

    def close(self):
        self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def send(self, cmd, data=b"", pkt_type=PKT_TYPE_CMD, err_code=0):
        body = bytes([
            STX0, STX1, pkt_type,
            cmd & 0xFF, (cmd >> 8) & 0xFF,
            err_code & 0xFF, (err_code >> 8) & 0xFF,
            len(data) & 0xFF, (len(data) >> 8) & 0xFF,
        ]) + bytes(data)

        checksum = (~sum(body) + 1) & 0xFF
        self.ser.write(body + bytes([checksum]))

    def recv(self):
        """응답 패킷 하나를 읽어 (cmd, err_code, data) 로 돌려준다."""
        def read(n):
            buf = self.ser.read(n)
            if len(buf) != n:
                raise TimeoutError("응답 timeout")
            return buf

        # STX 동기화
        while True:
            if read(1)[0] != STX0:
                continue
            if read(1)[0] == STX1:
                break

        head = read(7)
        _type = head[0]
        cmd = head[1] | (head[2] << 8)
        err_code = head[3] | (head[4] << 8)
        length = head[5] | (head[6] << 8)

        data = read(length) if length else b""
        checksum = read(1)[0]

        calc = (~sum(bytes([STX0, STX1]) + head + data) + 1) & 0xFF
        if calc != checksum:
            raise ValueError(f"checksum 불일치 : 수신 0x{checksum:02X}, 계산 0x{calc:02X}")

        return cmd, err_code, data

    def request(self, cmd, data=b""):
        """명령을 보내고 응답을 확인한다. 에러면 예외를 던진다."""
        self.ser.reset_input_buffer()
        self.send(cmd, data)
        resp_cmd, err_code, resp_data = self.recv()

        if resp_cmd != cmd:
            raise ValueError(f"응답 cmd 불일치 : 요청 0x{cmd:04X}, 응답 0x{resp_cmd:04X}")
        if err_code != 0:
            raise CmdError(cmd, err_code)

        return resp_data
