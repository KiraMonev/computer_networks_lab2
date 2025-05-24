FROM alpine:latest

RUN apk add --no-cache build-base

WORKDIR /app

COPY server/server.cpp ./
COPY server/httplib.h ./

RUN g++ -std=c++17 server.cpp -pthread -o server

EXPOSE 6000 8080

ENTRYPOINT ["./server"]
