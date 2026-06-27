/**
 * @file report_format.c
 * @brief Provides shared formatting helpers for text report sections.
 */
#include "report_internal.h"

/**
 * @brief Writes one key/value line to the report.
 */
void report_kv(FILE *f, const char *label, const char *fmt, ...) {
    fprintf(f, "* %-22s ", label);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
}

/**
 * @brief Formats a byte span as lowercase hexadecimal text.
 */
static void bytes_to_hex(const u8 *bytes, unsigned len, char *out, size_t out_size) {
    static const char hexdigits[] = "0123456789abcdef";
    if (!out_size) return;
    out[0] = 0;
    if (!bytes || out_size < len * 2u + 1u) return;
    for (unsigned i = 0; i < len; i++) {
        out[i * 2] = hexdigits[bytes[i] >> 4];
        out[i * 2 + 1] = hexdigits[bytes[i] & 0x0f];
    }
    out[len * 2] = 0;
}

/**
 * @brief Writes one digest value to the report.
 */
void report_digest(FILE *f, const char *base_name, const char *ext, const FileDigest *d, bool mib) {
    char md5[33], sha1[41], sha256[65];
    bytes_to_hex(d->md5, sizeof(d->md5), md5, sizeof(md5));
    bytes_to_hex(d->sha1, sizeof(d->sha1), sha1, sizeof(sha1));
    bytes_to_hex(d->sha256, sizeof(d->sha256), sha256, sizeof(sha256));

    report_kv(f, "File Name:", "%s%s", base_name, ext);
    if (mib) {
        u32 mib_x100 = (u32)((((u64)d->size * 100u) + 524288u) / 1048576u);
        report_kv(f, "File Size:", "%lu.%02lu MiB (%lu bytes)",
                  (unsigned long)(mib_x100 / 100u),
                  (unsigned long)(mib_x100 % 100u),
                  (unsigned long)d->size);
    } else {
        report_kv(f, "File Size:", "%lu bytes", (unsigned long)d->size);
    }
    report_kv(f, "CRC32:", "%08lx", (unsigned long)d->crc32);
    report_kv(f, "MD5:", "%s", md5);
    report_kv(f, "SHA-1:", "%s", sha1);
    report_kv(f, "SHA-256:", "%s", sha256);
}

/**
 * @brief Copies a fixed ROM header ASCII field into printable text.
 */
void report_copy_ascii_field(char *out, size_t out_size, const u8 *src, unsigned len) {
    if (!out_size) return;
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < out_size; i++) {
        unsigned char c = src[i];
        if (c == 0) break;
        out[n++] = (c >= 0x20u && c <= 0x7eu) ? (char)c : '?';
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

/**
 * @brief Formats a timestamp as an ISO-like report time.
 */
void report_format_iso_time(u32 seconds, char *out, size_t out_size) {
    if (!out_size) return;
    out[0] = 0;
    if (!seconds) {
        snprintf(out, out_size, "N/A");
        return;
    }

    time_t t = (time_t)seconds;
    struct tm *ltp = localtime(&t);
    struct tm *gtp = gmtime(&t);
    if (!ltp || !gtp) {
        snprintf(out, out_size, "N/A");
        return;
    }

    struct tm lt = *ltp;
    struct tm gt = *gtp;
    char date[32];
    if (!strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &lt)) {
        snprintf(out, out_size, "N/A");
        return;
    }

    long offset = (long)difftime(mktime(&lt), mktime(&gt));
    char sign = '+';
    if (offset < 0) {
        sign = '-';
        offset = -offset;
    }
    snprintf(out, out_size, "%s%c%02ld:%02ld", date, sign, offset / 3600, (offset / 60) % 60);
}

/**
 * @brief Formats elapsed transfer time for the report.
 */
void report_elapsed(FILE *f, const Download *dl, u32 bytes) {
    if (!dl->start_time || !dl->completion_time || dl->completion_time < dl->start_time) {
        report_kv(f, "Time Elapsed:", "N/A");
        return;
    }
    u32 elapsed = dl->completion_time - dl->start_time;
    if (!elapsed) {
        report_kv(f, "Time Elapsed:", "0 seconds");
        return;
    }
    u32 rate_x100 = (u32)((((u64)bytes * 100u) + ((u64)elapsed * 512u)) / ((u64)elapsed * 1024u));
    report_kv(f, "Time Elapsed:", "%lu seconds (%lu.%02lu KiB/s)",
              (unsigned long)elapsed,
              (unsigned long)(rate_x100 / 100u),
              (unsigned long)(rate_x100 % 100u));
}

/**
 * @brief Writes one possibly empty text metadata field to the report.
 */
void report_text_field(FILE *f, const char *label, const char *text) {
    fprintf(f, "* %-22s ", label);
    if (!text || !text[0]) {
        fputs("N/A\n", f);
        return;
    }
    const char *p = text;
    bool first = true;
    while (*p) {
        const char *e = strchr(p, '\n');
        if (!first) fputs(REPORT_VALUE_INDENT, f);
        if (e) {
            fwrite(p, 1, (size_t)(e - p), f);
            fputc('\n', f);
            p = e + 1;
        } else {
            fputs(p, f);
            fputc('\n', f);
            break;
        }
        first = false;
    }
}

/**
 * @brief Writes the optional RSA user-parameter block to the report.
 */
static void report_user_params(FILE *f, const u8 p[0x20]) {
    bool nonzero = false;
    for (unsigned i = 0; i < 0x20; i++) {
        if (p[i]) {
            nonzero = true;
            break;
        }
    }
    if (!nonzero) return;

    fputs("* User Parameters:\n", f);
    for (unsigned row = 0; row < 2; row++) {
        fprintf(f, "  %04X:", row * 0x10);
        for (unsigned i = 0; i < 0x10; i++) fprintf(f, " %02x", p[row * 0x10 + i]);
        fputc('\n', f);
    }
}

/**
 * @brief Writes RSA control-frame fields to the report.
 */
void report_rsa(FILE *f, const Download *dl) {
    if (dl->rsa_verification_skipped) {
        report_kv(f, "RSA Signature:", "Verification skipped (public key unavailable)");
    } else if (dl->rsa_hash_verified) {
        report_kv(f, "RSA Signature:", "Verified valid");
    } else {
        report_kv(f, "RSA Signature:", "Not verified");
    }
    report_kv(f, "ARM9 Entry Address:", "0x%lX", (unsigned long)dl->rsa.arm9_entrypoint);
    report_kv(f, "ARM7 Entry Address:", "0x%lX", (unsigned long)dl->rsa.arm7_entrypoint);
    report_kv(f, "Header Destination:", "0x%lX", (unsigned long)dl->sec[0].load_addr);
    report_kv(f, "Header Dest. (Temp):", "0x%lX", (unsigned long)dl->sec[0].staging_addr);
    report_kv(f, "Header Size:", "0x%lX bytes", (unsigned long)dl->sec[0].size);
    report_kv(f, "ARM9 Destination:", "0x%lX", (unsigned long)dl->sec[1].load_addr);
    report_kv(f, "ARM9 Dest. (Temp):", "0x%lX", (unsigned long)dl->sec[1].staging_addr);
    report_kv(f, "ARM9 Size:", "0x%lX bytes", (unsigned long)dl->sec[1].size);
    report_kv(f, "ARM7 Destination:", "0x%lX", (unsigned long)dl->sec[2].load_addr);
    report_kv(f, "ARM7 Dest. (Temp):", "0x%lX", (unsigned long)dl->sec[2].staging_addr);
    report_kv(f, "ARM7 Size:", "0x%lX bytes", (unsigned long)dl->sec[2].size);
    report_kv(f, "Section Flags:", "Header=%lu, ARM9=%lu, ARM7=%lu",
              (unsigned long)dl->sec[0].flags,
              (unsigned long)dl->sec[1].flags,
              (unsigned long)dl->sec[2].flags);
    report_user_params(f, dl->rsa.download_parameter);
}

/**
 * @brief Writes wireless capture statistics to the report.
 */
void report_wireless(FILE *f, const Download *dl) {
    const ContentSlot *slot = dl->slot;
    u8 ch = slot->bss.channel;
    if (ch >= 1 && ch <= 13) {
        report_kv(f, "Wi-Fi Channel:", "%u (%u MHz)", ch, 2407u + (unsigned)ch * 5u);
    } else {
        report_kv(f, "Wi-Fi Channel:", "N/A");
    }

    if (!dl->signal_sample_count) {
        report_kv(f, "Signal Strength Avg:", "N/A");
        report_kv(f, "Signal Strength Min:", "N/A");
        report_kv(f, "Signal Strength Max:", "N/A");
        return;
    }
    u32 avg_pct = (dl->signal_sum_percent + dl->signal_sample_count / 2u) / dl->signal_sample_count;
    u32 avg_raw = (dl->signal_raw_sum + dl->signal_sample_count / 2u) / dl->signal_sample_count;
    report_kv(f, "Signal Strength Avg:", "%lu%% (%lu RSSI)", (unsigned long)avg_pct, (unsigned long)avg_raw);
    report_kv(f, "Signal Strength Min:", "%u%% (%u RSSI)", dl->signal_min_percent, dl->signal_raw_min);
    report_kv(f, "Signal Strength Max:", "%u%% (%u RSSI)", dl->signal_max_percent, dl->signal_raw_max);
}

/**
 * @brief Writes protocol counters and completion status to the report.
 */
void report_protocol(FILE *f, const Download *dl) {
    const ProtocolStats *s = &dl->stats;
    u32 rsa_unique = s->rsa_packets_seen ? 1u : 0u;
    report_kv(f, "Malformed Commands:", "%lu", (unsigned long)s->malformed_commands);
    report_kv(f, "Commands Rejected:", "%lu", (unsigned long)s->commands_rejected);
    report_kv(f, "Packets Dropped:", "%lu", (unsigned long)s->packets_dropped);
    report_kv(f, "Bad File-No Packets:", "%lu", (unsigned long)s->bad_file_no_packets);
    report_kv(f, "Name Requests Seen:", "%lu", (unsigned long)s->name_requests_seen);
    report_kv(f, "RSA Packets Seen:", "%lu (%lu unique)", (unsigned long)s->rsa_packets_seen, (unsigned long)rsa_unique);
    report_kv(f, "Expected Data Packets:", "%lu", (unsigned long)s->expected_data_packets);
    report_kv(f, "Data Packets Seen:", "%lu (%lu unique)",
              (unsigned long)s->data_packets_seen,
              (unsigned long)s->unique_data_packets);
    report_kv(f, "Duplicate Data:", "%lu", (unsigned long)s->duplicate_data);
    report_kv(f, "Duplicate Identical:", "%lu", (unsigned long)s->duplicate_payload);
    report_kv(f, "Duplicate Different:", "%lu", (unsigned long)s->duplicate_different);
    report_kv(f, "Out-of-Range Data:", "%lu", (unsigned long)s->out_of_range_data);
    report_kv(f, "Short Data Packets:", "%lu", (unsigned long)s->short_data_packets);
    report_kv(f, "Final Commands Seen:", "%lu", (unsigned long)s->final_commands_seen);
    report_kv(f, "Got-All Replies Sent:", "%lu", (unsigned long)s->got_all_replies_sent);
    report_kv(f, "Data Replies Sent:", "%lu", (unsigned long)s->correction_replies_sent);
    report_kv(f, "Final Reply Sent:", "%lu", (unsigned long)s->final_replies_sent);
}
