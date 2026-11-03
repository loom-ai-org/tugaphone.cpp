// tugaphone.text_normalization: the orthographic rewrites that run before
// number verbalisation. Each function takes and returns UTF-8 text.
#pragma once

#include <string>
#include <string_view>

namespace tugaphone::text_normalization {

// "1139-1185" -> "1139 a 1185".
std::string split_number_ranges(std::string_view text);
// "92.073" -> "92073"; "10,4" -> "10 vírgula 4".
std::string normalize_number_separators(std::string_view text);
// "7kg" -> "7 kg"; "€20" -> "20 €"; "2,50 €" -> "2 € e 50 cêntimos"; "15x8" -> "15 vezes 8".
std::string separate_unit_symbols(std::string_view text);
// "16:00" -> "16 horas"; "16:54" -> "16 e 54".
std::string expand_clock_times(std::string_view text);
// "Sr. Silva" -> "Senhor Silva"; "D. Maria" -> "Dona Maria".
std::string expand_abbreviations(std::string_view text);
// "Afonso I" -> "Afonso Primeiro"; "o século XIX" -> "o século dezanove".
std::string expand_regnal_numerals(std::string_view text, std::string_view lang = "pt-PT");
// "IA" -> "i á".
std::string expand_acronyms(std::string_view text);
// "T2" -> "tê dois".
std::string expand_letter_digit_codes(std::string_view text, std::string_view lang = "pt-PT");
// Every rule above, in the order that keeps them from interfering.
std::string normalize_orthography(std::string_view text, std::string_view lang = "pt-PT");

}  // namespace tugaphone::text_normalization
