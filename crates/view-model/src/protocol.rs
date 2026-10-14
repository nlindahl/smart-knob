use crate::{MAX_BUNDLE, crc32};
pub const MAX_PAYLOAD: usize = 512;
pub const HELLO: u8 = 1;
pub const BEGIN: u8 = 2;
pub const CHUNK: u8 = 3;
pub const COMMIT: u8 = 4;
pub const SELECT: u8 = 5;
pub const STATE: u8 = 6;
pub const CALIBRATE: u8 = 7;
pub const TARE: u8 = 8;
pub const SAVE: u8 = 9;
pub const STOP: u8 = 10;
pub const ENABLE: u8 = 11;

#[derive(Clone, Debug, PartialEq)]
pub struct Frame {
    pub op: u8,
    pub seq: u16,
    pub payload: Vec<u8>,
}
impl Frame {
    pub fn encode(&self) -> Result<Vec<u8>, String> {
        if self.payload.len() > MAX_PAYLOAD {
            return Err("Payload too large".into());
        }
        let mut data = vec![b'S', b'K', 1, self.op];
        data.extend(self.seq.to_le_bytes());
        data.extend((self.payload.len() as u16).to_le_bytes());
        data.extend(&self.payload);
        data.extend(crc32(&data[2..]).to_le_bytes());
        Ok(data)
    }
    pub fn response(&self) -> Result<&[u8], String> {
        match self.payload.split_first() {
            Some((0, data)) => Ok(data),
            Some((code, _)) => Err(format!(
                "Device rejected command (status {code}: {})",
                match code {
                    1 => "invalid request",
                    2 => "busy",
                    3 => "invalid bundle",
                    4 => "storage failed",
                    5 => "hardware unavailable",
                    _ => "unknown",
                }
            )),
            None => Err("Empty device response".into()),
        }
    }
}
#[derive(Default)]
pub struct Parser {
    data: Vec<u8>,
}
impl Parser {
    pub fn feed(&mut self, bytes: &[u8]) -> Vec<Frame> {
        let mut frames = Vec::new();
        for b in bytes {
            self.data.push(*b);
            loop {
                if self.data.len() < 2 {
                    break;
                }
                if self.data[..2] != *b"SK" {
                    self.data.remove(0);
                    continue;
                }
                if self.data.len() < 8 {
                    break;
                }
                let len = usize::from(u16::from_le_bytes([self.data[6], self.data[7]]));
                if self.data[2] != 1 || len > MAX_PAYLOAD {
                    self.data.remove(0);
                    continue;
                }
                let end = len + 12;
                if self.data.len() < end {
                    break;
                }
                let got = u32::from_le_bytes(self.data[end - 4..end].try_into().unwrap());
                if got != crc32(&self.data[2..end - 4]) {
                    self.data.remove(0);
                    continue;
                }
                frames.push(Frame {
                    op: self.data[3],
                    seq: u16::from_le_bytes([self.data[4], self.data[5]]),
                    payload: self.data[8..8 + len].to_vec(),
                });
                self.data.drain(..end);
            }
        }
        frames
    }
}
pub fn upload_frames(bundle: &[u8], start_seq: u16) -> Result<Vec<Frame>, String> {
    if bundle.len() > MAX_BUNDLE || bundle.len() < 12 {
        return Err("Invalid bundle size".into());
    }
    let mut begin = (bundle.len() as u32).to_le_bytes().to_vec();
    begin.extend(crc32(bundle).to_le_bytes());
    let mut frames = vec![Frame {
        op: BEGIN,
        seq: start_seq,
        payload: begin,
    }];
    for (i, chunk) in bundle.chunks(MAX_PAYLOAD - 4).enumerate() {
        let mut payload = ((i * (MAX_PAYLOAD - 4)) as u32).to_le_bytes().to_vec();
        payload.extend(chunk);
        frames.push(Frame {
            op: CHUNK,
            seq: start_seq.wrapping_add(frames.len() as u16),
            payload,
        });
    }
    frames.push(Frame {
        op: COMMIT,
        seq: start_seq.wrapping_add(frames.len() as u16),
        payload: vec![],
    });
    Ok(frames)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragmented_and_corrupt_streams() {
        let f = Frame {
            op: 2,
            seq: 10,
            payload: vec![0, 83, 75, 255],
        };
        let b = f.encode().unwrap();
        let mut p = Parser::default();
        let mut bad = b.clone();
        bad[8] ^= 1;
        assert!(p.feed(&bad).is_empty());
        let mut result = vec![];
        for byte in b {
            result.extend(p.feed(&[byte]));
        }
        assert_eq!(result, vec![f]);
    }
    #[test]
    fn upload_offsets() {
        let bundle = vec![3; 2000];
        let fs = upload_frames(&bundle, u16::MAX).unwrap();
        assert_eq!(fs[1].seq, 0);
        assert_eq!(fs[2].payload[..4], 508u32.to_le_bytes());
        assert_eq!(fs.last().unwrap().op, COMMIT);
    }
}
