| model | context window | tokens per second on the M5 Max | tool calling | notes about long-context behaviour |
|---|---|---|---|---|
| qwen3-next-80b-a3b-instruct-4bit | 262144 | 41.7 | yes, grammar-constrained | chunked prefill must be disabled |
| llama-3.3-70b-instruct-4bit | 131072 | 12.3 | no | fine |
