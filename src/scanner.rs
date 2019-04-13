use std::iter::Peekable;
use std::str::CharIndices;

// TODO
#[derive(Clone)]
pub struct Name {
    str: String,
}

#[derive(Clone)]
pub enum Token {
    Ident(Name),
    Newline,
    Arrow,
    Quote,
    DQuote,
    Lparen,
    Rparen,
    Eof,
    Err,
}

pub struct TokenAndPos {
    pub tok: Token,
    pos: usize,
}

pub struct Scanner<'a> {
    ch: char,
    pos: usize,   // current lookahead position
    pivot: usize, // start of the currently scanned token
    line_offs: Vec<usize>,
    iter: Peekable<CharIndices<'a>>,
}

// NOTE: Longer strings should come first, as this map is subject to front-to-rear linear search,
// and the search may terminate prematurely if a shorter substring matches the given string first.
const TOKEN_STR_MAP: &[(&str, Token)] = &[
    ("->", Token::Arrow),
    ("\"", Token::DQuote),
    ("\n", Token::Newline),
    ("'", Token::Quote),
    ("(", Token::Lparen),
    (")", Token::Rparen),
];

impl<'a> Scanner<'a> {
    pub fn from_string(src: &str) -> Scanner {
        let mut l = Scanner {
            ch: '\0',
            pos: 0,
            pivot: 0,
            line_offs: Vec::new(),
            iter: src.char_indices().peekable(),
        };
        l.cache();
        l
    }

    fn cache(&mut self) {
        match self.iter.peek() {
            Some(&(pos, ch)) => {
                self.pos = pos;
                self.ch = ch;
                if self.ch == '\n' {
                    self.line_offs.push(self.pos);
                }
            }
            None => {
                // self.pos = self.src.len();
                self.ch = '\0'; // EOF
            }
        }
    }

    fn bump(&mut self) {
        self.iter.next();
        self.cache();
    }

    fn lookn(&mut self, n: usize) -> Option<char> {
        match self.iter.nth(n) {
            Some((_, ch)) => Some(ch),
            None => None,
        }
    }

    fn is_end(&self) -> bool {
        self.ch == '\0'
    }

    fn skip_while(&mut self, f: &Fn(char) -> bool) {
        while !self.is_end() && f(self.ch) {
            self.bump();
        }
    }

    fn take_while(&mut self, f: &Fn(char) -> bool) -> String {
        let mut s = "".to_string();
        while !self.is_end() && f(self.ch) {
            s.push(self.ch);
            self.bump();
        }
        s
    }

    fn skip_whitespace(&mut self) {
        self.skip_while(&|ch: char| ch.is_whitespace());
    }

    fn scan_ident(&mut self) -> TokenAndPos {
        let s = self.take_while(&|ch: char| ch.is_alphanumeric() || ch == '_');
        println!("scan_ident: [{}]", s);
        TokenAndPos {
            tok: Token::Ident(Name { str: s }),
            pos: self.pos,
        }
    }

    fn scan_symbol(&mut self) -> TokenAndPos {
        // Should be careful about comparing against multi-char symbols as the source string may
        // terminate early.

        'cand: for (s, tok) in TOKEN_STR_MAP {
            let mut cand_iter = s.chars();
            let mut iter = self.iter.clone();

            loop {
                let ch_cand: char;
                match cand_iter.next() {
                    Some(ch) => ch_cand = ch,
                    None => {
                        // Termination of candidate string means a complete match.  The
                        // longest-to-front rule for TOKEN_STR_MAP guarantees that no other
                        // candidate can match the source string with more characters.
                        self.iter = iter;
                        self.cache();
                        return TokenAndPos {tok: tok.clone(), pos: self.pos};
                    }
                }

                let ch_src: char;
                match iter.next() {
                    Some((_, ch)) => ch_src = ch,
                    None => {
                        // Termination of the source string means a failed match.
                        continue 'cand;
                    }
                }

                if ch_src != ch_cand {
                    continue 'cand;
                }
            }
        }

        println!("err on [{}]", self.ch);
        TokenAndPos {
            tok: Token::Err,
            pos: self.pos,
        }
    }

    pub fn scan(&mut self) -> TokenAndPos {
        self.skip_whitespace();

        if self.is_end() {
            return TokenAndPos {
                tok: Token::Eof,
                pos: self.pos,
            };
        }

        match self.ch {
            '"' => TokenAndPos {
                tok: Token::Err,
                pos: self.pos,
            },
            '/' => TokenAndPos {
                tok: Token::Err,
                pos: self.pos,
            },
            ch => {
                if ch.is_alphabetic() || ch == '_' {
                    self.scan_ident()
                } else {
                    self.scan_symbol()
                }
            }
        }
    }
}
