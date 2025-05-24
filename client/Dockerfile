FROM alpine:latest

RUN apk add --no-cache build-base

WORKDIR /app
COPY client/client.cpp ./
RUN g++ -std=c++17 client.cpp -o client

ENTRYPOINT ["./client"]
