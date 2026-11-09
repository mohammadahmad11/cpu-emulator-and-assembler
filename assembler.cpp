#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdint>

// Portable compile-time endianness detection for C++17
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    constexpr bool is_host_little_endian = false;
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    constexpr bool is_host_little_endian = true;
#elif defined(_WIN32) // MSVC on Windows (x86/x64/ARM are little-endian)
    constexpr bool is_host_little_endian = true;
#else
    #error "Cannot determine host endianness"
#endif

// Portable 32-bit byte swap
constexpr uint32_t swap32(uint32_t val) noexcept {
    return ((val & 0x000000FFu) << 24) |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0xFF000000u) >> 24);
}

// Convert host integer to big endian (and vice versa)
constexpr uint32_t hostToBigEndian(uint32_t val) noexcept {
    if constexpr (is_host_little_endian) {
        return swap32(val);
    }
    return val;
}

constexpr uint32_t bigEndianToHost(uint32_t val) noexcept {
    return hostToBigEndian(val); // Symmetric transformation
}

bool writeBigEndianBinary(const std::string& filename, const std::vector<uint32_t>& data) {
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        return false;
    }

    std::vector<uint32_t> buffer;
    buffer.reserve(data.size());

    // Convert values to big-endian
    for (uint32_t val : data) {
        buffer.push_back(hostToBigEndian(val));
    }

    // Single bulk-write operation
    outFile.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() * sizeof(uint32_t));
    return outFile.good();
}

using namespace std;
vector<string> symbols = {};
map<string,int> symbol_to_addr = {};
int next_id = 1;
map<int,vector<vector<string>>> id_to_content = {};
int line_no;
string mode;
vector<vector<string>> combined;
vector<uint32_t> bin;

string errors = "";

vector<string> split_statement(string statement) {
	std::stringstream ss(statement);
    std::string token;
    std::vector<std::string> tokens;

    // Standard stream extraction >> automatically skips leading, consecutive,
    // and trailing whitespace (spaces, tabs, newlines).
    while (ss >> token) {
        tokens.push_back(token);
    }

    return tokens;
}

string lowercase(string input) {
	for (char &c : input) {
		c = tolower(static_cast<unsigned char>(c));
	}
	return input;
}

void mkerror(string file_name, int line_no, string message) {
	errors += file_name + ":" + to_string(line_no) + ": " + message + "\n";
}

bool is_label(string input) {
	if (input.size() >= 2) {
		if (input[0] == '.') {
			return true;
		}
	}
	return false;
}

bool is_hex(string input) {
	if (input.size() == 8) {
		if (input.substr(0,2) == "0x") {
			input = input.substr(2,6);
			bool condition = true;
			for (auto digit : input) {
				if (isdigit(digit)) {
					
				} else if (isalpha(digit) && digit >= 'a' && digit <= 'f') {
					
				} else {
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

bool is_reg(string input) {
	if (input.size() == 2) {
		if (input[0] == 'r') {
			if (isdigit(input[1])) {
				if (stoi(string(1, input[1])) <= 7) {
					return true;
				}
			}
		}
	}
	return false;
}

int main(int argc, const char* argv[]) {
    
	argc = 2;
	argv[1] = "myasm.txt";
	argv[2] = "myprog";
    if (argc < 2) {
        cout << "Usage: " << argv[0] << "  [file_path_2 ...]" << endl;
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        string filePath = argv[i];
        ifstream file(filePath);

        cout << "=== Reading file: " << filePath << " ===" << endl;

        if (!file.is_open()) {
            cerr << "Error: Could not open file " << filePath << endl;
            cout << endl;
            continue;
        }

		string file_name = string(filePath);
		mode = "null";
        string line;
		line_no = 0;
		id_to_content[next_id] = {};

        while (getline(file, line)) {
			line = lowercase(line);
		 	auto splitted_stat = split_statement(line);
			id_to_content[next_id].push_back(splitted_stat);
            if (mode == "null" && line_no == 0 && line != "data" && line != "code") {
				mkerror(file_name, line_no, "mode not initiated");
			} else if (line == "data" || line  == "code") {
				mode = line;
			} else if (line != "") {
				bool pub = false;
				string label = "";
				int label_rel_err = 0;
				if (splitted_stat.size() >= 1) {
					if (splitted_stat[0] == "pub") {
						pub = true;
					}
					if (pub && splitted_stat.size() < 2) {
						mkerror(file_name, line_no, "no label given");
						label_rel_err = -1;
					} else if (pub && splitted_stat[1].size() < 2) {
						mkerror(file_name, line_no, "label not according to format");
						label_rel_err = -1;
					} else if (pub && splitted_stat[1][0] != '.') {
						mkerror(file_name, line_no, "labels start with a '.'");
						label_rel_err = -1;
					} else {
						if (pub) {
							label = splitted_stat[1];
							label_rel_err = 2;
						} else {
							if (splitted_stat[0][0] == '.') {
								label = splitted_stat[0];
								label_rel_err = 1;
							}
						}
					}

					if (label != "") {
						string priv_label = to_string(next_id) + label;
						if (!pub) {
							label = priv_label;
							if (find(symbols.begin(), symbols.end(), priv_label) != symbols.end()) {
								mkerror(file_name, line_no, "symbol name already used");
								label_rel_err = -1;
							}
						} else {
							if (find(symbols.begin(), symbols.end(), priv_label) != symbols.end() || find(symbols.begin(), symbols.end(), label) != symbols.end()) {
								mkerror(file_name, line_no, "symbol name already used");
								label_rel_err = -1;

							}
						}
						if (label_rel_err != -1) {
							symbols.push_back(label);
						}
					}

					if (label_rel_err != -1) {
						if (splitted_stat.size() > label_rel_err) {
                            int strt = label_rel_err;
                            if (mode == "data") {
                                if (splitted_stat.size() > strt + 1) {
                                    mkerror(file_name, line_no, "invalid data");
                                } else {
                                    string the_hex = splitted_stat[strt];
                                    if (the_hex.size() != 8) {
                                        mkerror(file_name, line_no, "just 6 hexdigits prefixed by '0x'");
                                    } else if (the_hex.substr(0,2) != "0x") {
                                        mkerror(file_name, line_no, "it has to be prefixed by '0x'");
                                    } else {
                                        the_hex = the_hex.substr(2,6);
                                        // get to work here
                                    }
                                }
                            } else {
                                string instr = splitted_stat[strt];
                                if (instr == "nop") {
									if (splitted_stat.size() == strt + 1) {
										id_to_content[next_id][line_no][strt] = "nop0";
                                    } else {
										mkerror(file_name, line_no, "nop doesnt take any operands");
									}

                                } else if (instr == "br") {
									string err_msg = "proper syntax is: br [imm] [reg/mem]";
									bool condition = false;
									if (splitted_stat.size() == strt + 3) {
										if (is_hex(splitted_stat[strt+1]) && (is_reg(splitted_stat[strt+2]) || is_label(splitted_stat[strt+2]))) {
											condition = true;
											if (is_reg(splitted_stat[strt+2])) {
												id_to_content[next_id][line_no][strt] = "br0";
											} else {
												id_to_content[next_id][line_no][strt] = "br1";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "add") {
									string err_msg = "proper syntax is: add [reg] [reg] [reg/imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 4) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2]) && (is_reg(splitted_stat[strt+3]) || is_hex(splitted_stat[strt+3]))) {
											condition = true;
											if (is_reg(splitted_stat[strt+3])) {
												id_to_content[next_id][line_no][strt] = "add0";
											} else {
												id_to_content[next_id][line_no][strt] = "add1";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "sub") {
									string err_msg = "proper syntax is: sub [reg] [reg] [reg/imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 4) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2]) && (is_reg(splitted_stat[strt+3]) || is_hex(splitted_stat[strt+3]))) {
											condition = true;
											if (is_reg(splitted_stat[strt+3])) {
												id_to_content[next_id][line_no][strt] = "sub0";
											} else {
												id_to_content[next_id][line_no][strt] = "sub1";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "st") {
									string err_msg = "proper syntax is: st [mem] [reg]";
									bool condition = false;
									if (splitted_stat.size() == strt + 3) {
										if (is_label(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "st0";
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}
									
                                } else if (instr == "ld") {
									string err_msg = "proper syntax is: ld [reg] [reg/mem/imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 3) {
										if (is_label(splitted_stat[strt+1]) && (is_reg(splitted_stat[strt+2])) || is_label(splitted_stat[strt+2]) || is_hex(splitted_stat[strt+2])) {
											condition = true;
											if (is_reg(splitted_stat[strt+2])) {
												id_to_content[next_id][line_no][strt] = "ld0";
											} else if (is_label(splitted_stat[strt+2])) {
												id_to_content[next_id][line_no][strt] = "ld1";
											} else {
												id_to_content[next_id][line_no][strt] = "ld2";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "lea") {
									string err_msg = "proper syntax is: lea [reg] [mem]";
									bool condition = false;
									if (splitted_stat.size() == strt + 3) {
										if (is_reg(splitted_stat[strt+1]) && is_label(splitted_stat[strt+2])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "lea0";
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

								} else if (instr == "or") {
									string err_msg = "proper syntax is: or [reg] [reg] [reg/imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 4) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2]) && (is_reg(splitted_stat[strt+3]) || is_hex(splitted_stat[strt+3]))) {
											if (is_reg(splitted_stat[strt+3])) {
												condition = true;
												id_to_content[next_id][line_no][strt] = "or0";
											} else {
												condition = true;
												id_to_content[next_id][line_no][strt] = "or1";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "and") {
									string err_msg = "proper syntax is: and [reg] [reg] [reg/imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 4) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2]) && (is_reg(splitted_stat[strt+3]) || is_hex(splitted_stat[strt+3]))) {
											if (is_reg(splitted_stat[strt+3])) {
												condition = true;
												id_to_content[next_id][line_no][strt] = "and0";
											} else {
												condition = true;
												id_to_content[next_id][line_no][strt] = "and1";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "ldr") {
									string err_msg = "proper syntax is: ldr [reg] [reg]";
									bool condition = false;
									if (splitted_stat.size() == strt + 3) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "ldr0";
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "str") {
									string err_msg = "proper syntax is: str [reg] [reg]";
									bool condition = false;
									if (splitted_stat.size() == strt + 3) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "str0";
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "shl") {
									string err_msg = "proper syntax is: shl [reg] [reg] [reg/imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 4) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2]) && (is_reg(splitted_stat[strt+3]) || is_hex(splitted_stat[strt+3]))) {
											if (is_reg(splitted_stat[strt+3])) {
												condition = true;
												id_to_content[next_id][line_no][strt] = "shl0";
											} else {
												condition = true;
												id_to_content[next_id][line_no][strt] = "shl1";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "not") {
									string err_msg = "proper syntax is: not [reg] [reg]";
									bool condition = false;
									if (splitted_stat.size() == strt + 3) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "not0";
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "jmp") {
									string err_msg = "proper syntax is: jmp [reg/mem]";
									bool condition = false;
									if (splitted_stat.size() == strt + 2) {

										if (is_reg(splitted_stat[strt+1])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "jmp0";
										} else if (is_label(splitted_stat[strt+1])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "jmp1";
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}
                                } else if (instr == "shr") {
									string err_msg = "proper syntax is: shr [reg] [reg] [reg/imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 4) {
										if (is_reg(splitted_stat[strt+1]) && is_reg(splitted_stat[strt+2]) && (is_reg(splitted_stat[strt+3]) || is_hex(splitted_stat[strt+3]))) {
											if (is_reg(splitted_stat[strt+3])) {
												condition = true;
												id_to_content[next_id][line_no][strt] = "shr0";
											} else {
												condition = true;
												id_to_content[next_id][line_no][strt] = "shr1";
											}
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}

                                } else if (instr == "trap") {
									string err_msg = "proper syntax is: trap [imm]";
									bool condition = false;
									if (splitted_stat.size() == strt + 2) {
										if (is_hex(splitted_stat[strt+1])) {
											condition = true;
											id_to_content[next_id][line_no][strt] = "trap0";
										}
									}
									if (!condition) {
										mkerror(file_name, line_no, err_msg);
									}
                                } else {
                                    mkerror(file_name, line_no, "invalid instruction");
                                }
                            }
						} else {
							mkerror(file_name, line_no, "label cannot be on an empty line");
						}
					}
				}
				
			}
			
			



			line_no++;
        }

        next_id++;
        file.close();
        cout << endl;
    }

	if (errors == "") {
		for (int id = 1; id < next_id; id++) {
			string str_id = to_string(id);
			string file_name = argv[id];
			auto& content = id_to_content[id];
			int line_no = 0;
			for (auto& a_line : content) {
				for (auto& a_word : a_line) {
					if (is_label(a_word)) {
						string priv_label = str_id + a_word;
						if (find(symbols.begin(), symbols.end(), priv_label) != symbols.end()) { 
							a_word = priv_label;
						} else if (find(symbols.begin(), symbols.end(), a_word) == symbols.end()) {
							mkerror(file_name, line_no, "symbol not found");
						}
					}
				}
				if (a_line != vector<string>({}) && a_line != vector<string>{"data"} && a_line != vector<string>{"code"}) {
					combined.push_back(a_line);
				}
				line_no++;
			}
		}
	}

	if (errors == "") {
		combined.insert(combined.begin(), {"jmp1", ".main"});
		combined.insert(combined.begin(), {"code"});
		int line_no = 0;
		for (auto& a_line : combined) {
			string a_symbol = "";
			bool condition = true;
			if (a_line[0] == "pub") {
				a_symbol = a_line[1];
				a_line.erase(a_line.begin());
				a_line.erase(a_line.begin());
			} else if (is_label(a_line[0])) {
				a_symbol = a_line[0];
				a_line.erase(a_line.begin());
			}
			if (a_symbol != "") {
				symbol_to_addr[a_symbol] = line_no;
			}
			line_no++;
		}

		for (auto& a_line : combined) {
			stringstream ss;
			uint32_t cell;
			if (!is_hex(a_line[0])) {
				if (a_line[0] == "nop0") {
					cell = 0;

				} else if (a_line[0] == "br0") {
					cell = 0x1 << 28;
					uint32_t cond;
					ss << hex << a_line[1];
					ss >> cond;
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					cell = cell | (cond << 25) | (r1 << 21);
					
				} else if (a_line[0] == "br1") {
					cell = 0x1 << 28;
					uint32_t cond;
					ss << hex << a_line[1];
					ss >> cond;
					uint32_t addr = symbol_to_addr[a_line[2]];
					cell = cell | (cond << 25) | (0x1 << 24) | addr;

				} else if (a_line[0] == "add0") {
					cell = 0x2 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t r2 = stoi(a_line[3].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22) | r2;

				} else if (a_line[0] == "add1") {
					cell = 0x2<< 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t imm;
					ss << hex << a_line[3];
					ss >> imm;
					cell = cell | (r0 << 25) | (r1 << 22) | (0x1 << 21) | imm;

				} else if (a_line[0] == "sub0") {
					cell = 0x3 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t r2 = stoi(a_line[3].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22) | r2;

				} else if (a_line[0] == "sub1") {
					cell = 0x3 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t imm;
					ss << hex << a_line[3];
					ss >> imm;
					cell = cell | (r0 << 25) | (r1 << 22) | (0x1 << 21) | imm;

				} else if (a_line[0] == "st0") {
					cell = 0x4<< 28;
					uint32_t addr = symbol_to_addr[a_line[1]];
					uint32_t r0 = stoi(a_line[2].substr(1,1));
					cell = cell | (r0 << 25) | addr;

				} else if (a_line[0] == "ld0") {
					cell = 0x5 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 20);

				} else if (a_line[0] == "ld1") {
					cell = 0x5 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t addr = symbol_to_addr[a_line[2]];
					cell = cell | (r0 << 25) | (0x1 << 23) |  addr;
					
				} else if (a_line[0] == "ld2") {
					cell = 0x5 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t imm;
					ss << hex << a_line[2];
					ss >> imm;
					cell = cell | (r0 << 25) | (0x10 << 23) | imm;
					
				} else if (a_line[0] == "lea0") {
					cell = 0x5 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t addr = symbol_to_addr[a_line[2]];
					cell = cell | (r0 << 25) | (0x10 << 23) | addr;

				} else if (a_line[0] == "or0") {
					cell = 0x6 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t r2 = stoi(a_line[3].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22) | r2;

				} else if (a_line[0] == "or1") {
					cell = 0x6 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t imm;
					ss << hex << a_line[3];
					ss >> imm;
					cell = cell | (r0 << 25) | (r1 << 22) | imm;

				} else if (a_line[0] == "and0") {
					cell = 0x7 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t r2 = stoi(a_line[3].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22) | r2;

				} else if (a_line[0] == "and1") {
					cell = 0x7 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t imm;
					ss << hex << a_line[3];
					ss >> imm;
					cell = cell | (r0 << 25) | (r1 << 22) | imm;

				} else if (a_line[0] == "ldr0") {
					cell = 0x8 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22);

				} else if (a_line[0] == "str0") {
					cell = 0x9 << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22);

				} else if (a_line[0] == "shl0") {
					cell = 0xa << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t r2 = stoi(a_line[3].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22) | r2;

				} else if (a_line[0] == "shl1") {
					cell = 0xa << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t imm;
					ss << hex << a_line[3];
					ss >> imm;
					cell = cell | (r0 << 25) | (r1 << 22) | imm;

				} else if (a_line[0] == "not0") {
					cell = 0xb << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22);

				} else if (a_line[0] == "jmp0") {
					cell = 0xc << 28;
					uint32_t r1 = stoi(a_line[1].substr(1,1));
					cell = cell | (r1 << 24);

				} else if (a_line[0] == "jmp1") {
					cell = 0xc << 28;
					uint32_t addr = symbol_to_addr[a_line[1]];
					cell = cell | addr;

				} else if (a_line[0] == "shr0") {
					cell = 0xd << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t r2 = stoi(a_line[3].substr(1,1));
					cell = cell | (r0 << 25) | (r1 << 22) | r2;

				} else if (a_line[0] == "shr1") {
					cell = 0xd << 28;
					uint32_t r0 = stoi(a_line[1].substr(1,1));
					uint32_t r1 = stoi(a_line[2].substr(1,1));
					uint32_t imm;
					ss << hex << a_line[3];
					ss >> imm;
					cell = cell | (r0 << 25) | (r1 << 22) | imm;

				} else if (a_line[0] == "trap0") {
					cell = 0xe << 28;
					uint32_t trap_code;
					ss << hex << a_line[1];
					ss >> trap_code;
					cell = cell | trap_code;

				} 

			} else {
				ss << hex << a_line[0];
				ss >> cell;
			}
			bin.push_back(cell);

			writeBigEndianBinary(string(argv[argc]), bin);
		}
	} else {
		cout << errors;
	}


    return 0;
}
