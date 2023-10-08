<a name="readme-top"></a>

<!-- PROJECT LOGO -->
<br />
<div align="center">
    <img src="artifacts/logo.png" alt="Logo" width="150" height="150">
    <h1>Remote File Upload Tool</h1>
  </a>
</div>

<!-- ABOUT THE PROJECT -->
## About The Project
This project is a low-level networking application using a multiprocessing client-server model. Clients can simultaneously connect to the server and send their file packets over the network and ultimately upload their file data to the server.

This project was intentionally developed using unreliable UDP connections in order to mimic real world scenarios of packet loss and corruption. I developed a working selective reject flow control protocol that can successfully deal with dropped and corrupted packets and still successfully complete file uploads.


### Built With

* ![C-URL]

## Installation and Usage
* Compilation/Execution Instructions
	* cd into `src` directory
	* `make`
	* `./server <error-rate> <optional-port-number>`
	* `./rcopy <local-filename> <destination-file-name> <window-size> <buffer-size> <error-rate> <remote-host> <remote-port>`
* Demo: 

<!-- MARKDOWN LINKS & IMAGES -->
[C-URL]: https://img.shields.io/badge/c-%2300599C.svg?style=for-the-badge&logo=c&logoColor=white
[HTML-URL]: https://img.shields.io/badge/html5-%23E34F26.svg?style=for-the-badge&logo=html5&logoColor=white
[CSS-URL]: https://img.shields.io/badge/css3-%231572B6.svg?style=for-the-badge&logo=css3&logoColor=white

<p align="right">(<a href="#readme-top">back to top</a>)</p>