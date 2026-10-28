/******************************************************************************
   Equilibrium Outpost - WALKIE TALKIE SERVER (Node.js + Express)

   WHO MADE IT : OUR OWN MODULE (team 3), made by a teammate.
   WHAT IT DOES: serves the two walkie talkie pages in the 'public' folder:
                   http://localhost:3000/walkie_laptop_sender.html   (laptop)
                   http://<laptop IP>:3000/walkie_phone_receiver.html (phone)
   NOTE        : the pages now set up their WebRTC connection over OOCSI
                 (channel OOCSI-things/team-3-walkie). The Socket.IO part below
                 is from the first version and is no longer needed, but it does
                 no harm, so it is kept.
   START       : npm install   (once)   then   node server.js
 ******************************************************************************/

const express = require('express');
const http = require('http');
const { Server } = require('socket.io');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

// serve the files from the 'public' folder (the two walkie talkie pages)
app.use(express.static('public'));

// old signalling (first version): passes a signal on to the other devices
io.on('connection', (socket) => {
    console.log('A device is connected:', socket.id);

    socket.on('signal', (data) => {
        socket.broadcast.emit('signal', data);
    });

    socket.on('disconnect', () => {
        console.log('Device disconnected:', socket.id);
    });
});

// start the server on port 3000
const PORT = process.env.PORT || 3000;
server.listen(PORT, () => {
    console.log(`Server established! Open http://localhost:${PORT}/walkie_laptop_sender.html on the laptop.`);
});
